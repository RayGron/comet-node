#include "run/launcher_run_service.h"

#include <algorithm>
#include <chrono>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
#include <mutex>

#include <nlohmann/json.hpp>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "naim/core/platform_compat.h"
#include "naim/security/crypto_utils.h"
#include "naim/state/sqlite_store.h"
#include "app/hostd_controller_transport_support.h"
#include "app/hostd_reporting_support.h"
#include "backend/hostd_backend_factory.h"
#include "run/hostd_peer_service.h"

namespace naim::launcher {

namespace {

bool IsIpv4Address(const std::string& candidate) {
  int octet_count = 0;
  std::string octet;
  std::istringstream input(candidate);
  while (std::getline(input, octet, '.')) {
    if (octet.empty() || octet.size() > 3) {
      return false;
    }
    for (const char ch : octet) {
      if (!std::isdigit(static_cast<unsigned char>(ch))) {
        return false;
      }
    }
    const int value = std::stoi(octet);
    if (value < 0 || value > 255) {
      return false;
    }
    ++octet_count;
  }
  return octet_count == 4;
}

#if !defined(_WIN32)
int ProcessExitCodeFromStatus(int status) {
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    return 128 + WTERMSIG(status);
  }
  return 1;
}

std::optional<int> PollChildExitCode(pid_t pid) {
  int status = 0;
  const pid_t result = waitpid(pid, &status, WNOHANG);
  if (result == 0) {
    return std::nullopt;
  }
  if (result == pid) {
    return ProcessExitCodeFromStatus(status);
  }
  if (result < 0 && errno == ECHILD) {
    return 0;
  }
  return 1;
}

void StopChildProcess(pid_t pid) {
  if (pid <= 0) {
    return;
  }
  if (PollChildExitCode(pid).has_value()) {
    return;
  }
  kill(pid, SIGTERM);
  for (int attempt = 0; attempt < 50; ++attempt) {
    if (PollChildExitCode(pid).has_value()) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  kill(pid, SIGKILL);
  while (!PollChildExitCode(pid).has_value()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}
#endif

class LauncherTelemetryBackendSupport final : public naim::hostd::IHttpHostdBackendSupport {
 public:
  nlohmann::json SendControllerJsonRequest(
      const std::string& controller_url,
      const std::string& method,
      const std::string& path,
      const nlohmann::json& payload,
      const std::map<std::string, std::string>& headers = {}) const override {
    return naim::hostd::controller_transport_support::SendControllerJsonRequest(
        controller_url,
        method,
        path,
        payload,
        headers);
  }

  naim::HostAssignment ParseAssignmentPayload(const nlohmann::json& payload) const override {
    return naim::hostd::controller_transport_support::ParseAssignmentPayload(payload);
  }

  nlohmann::json BuildHostObservationPayload(
      const naim::HostObservation& observation) const override {
    return naim::hostd::controller_transport_support::BuildHostObservationPayload(observation);
  }

  nlohmann::json BuildHostTelemetryPayload(
      const naim::HostTelemetryFrame& frame) const override {
    return naim::hostd::controller_transport_support::BuildHostTelemetryPayload(frame);
  }

  nlohmann::json BuildDiskRuntimeStatePayload(
      const naim::DiskRuntimeState& state) const override {
    return naim::hostd::controller_transport_support::BuildDiskRuntimeStatePayload(state);
  }

  naim::DiskRuntimeState ParseDiskRuntimeStatePayload(
      const nlohmann::json& payload) const override {
    return naim::hostd::controller_transport_support::ParseDiskRuntimeStatePayload(payload);
  }

  std::string Trim(const std::string& value) const override {
    return naim::hostd::controller_transport_support::Trim(value);
  }
};

struct LauncherTelemetryBusItem {
  naim::HostTelemetryFrame frame;
  std::chrono::steady_clock::time_point enqueued_at;
};

class LauncherTelemetryBus final {
 public:
  void Publish(naim::HostTelemetryFrame frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= kCapacity) {
      queue_.pop_front();
      ++dropped_frames_;
    }
    queue_.push_back(LauncherTelemetryBusItem{std::move(frame), std::chrono::steady_clock::now()});
    cv_.notify_one();
  }

  std::optional<LauncherTelemetryBusItem> WaitPop(const std::atomic_bool& stop_requested) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, std::chrono::milliseconds(250), [&]() {
      return stop_requested.load() || !queue_.empty();
    });
    if (queue_.empty()) {
      return std::nullopt;
    }
    auto item = std::move(queue_.front());
    queue_.pop_front();
    return item;
  }

  void Stop() {
    cv_.notify_all();
  }

  std::uint64_t DroppedFrames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_frames_;
  }

  std::size_t Depth() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

 private:
  static constexpr std::size_t kCapacity = 16;

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<LauncherTelemetryBusItem> queue_;
  std::uint64_t dropped_frames_ = 0;
};

struct LauncherTelemetryRuntimeMetrics {
  std::atomic<std::uint64_t> last_publish_duration_ms{0};
  std::atomic<std::uint64_t> publish_error_count{0};
  std::string last_publish_error;
  mutable std::mutex error_mutex;
};

std::uint64_t DurationMillis(
    const std::chrono::steady_clock::time_point started_at,
    const std::chrono::steady_clock::time_point finished_at) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(finished_at - started_at).count());
}

struct LauncherTelemetryCadence {
  std::chrono::milliseconds interval;
  std::string reason;
};

LauncherTelemetryCadence ResolveTelemetryCadence(const naim::HostTelemetryFrame& frame) {
  if (frame.plane_name.empty() && frame.instance_runtime.empty()) {
    return {std::chrono::seconds(10), "idle-no-plane"};
  }
  const bool has_not_ready = std::any_of(
      frame.instance_runtime.begin(),
      frame.instance_runtime.end(),
      [](const naim::RuntimeProcessStatus& status) {
        return !status.ready || status.runtime_phase == "starting" ||
               status.runtime_phase == "deploying" || status.runtime_phase == "loading";
      });
  if (has_not_ready) {
    return {std::chrono::seconds(1), "plane-changing"};
  }
  if (!frame.instance_runtime.empty()) {
    return {std::chrono::seconds(5), "stable-runtime"};
  }
  return {std::chrono::seconds(3), "plane-without-runtime"};
}

std::string LoadLastPublishError(const LauncherTelemetryRuntimeMetrics& metrics) {
  std::lock_guard<std::mutex> lock(metrics.error_mutex);
  return metrics.last_publish_error;
}

void StoreLastPublishError(
    LauncherTelemetryRuntimeMetrics* metrics,
    const std::string& error) {
  if (metrics == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(metrics->error_mutex);
  metrics->last_publish_error = error;
}

std::filesystem::path HostdSelfUpdateMarkerPath(
    const std::filesystem::path& state_root,
    const std::string& node_name) {
  return state_root / "control" / ("hostd-self-update-scheduled-" + node_name);
}

bool IsPrivateIpv4Address(const std::string& candidate) {
  if (!IsIpv4Address(candidate)) {
    return false;
  }
  std::istringstream input(candidate);
  std::string octet_text;
  std::vector<int> octets;
  while (std::getline(input, octet_text, '.')) {
    octets.push_back(std::stoi(octet_text));
  }
  if (octets.size() != 4) {
    return false;
  }
  return octets[0] == 10 ||
         (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31) ||
         (octets[0] == 192 && octets[1] == 168);
}

bool SetEnvVar(const std::string& key, const std::string& value) {
#if defined(_WIN32)
  return _putenv_s(key.c_str(), value.c_str()) == 0;
#else
  return ::setenv(key.c_str(), value.c_str(), 1) == 0;
#endif
}

std::string LoadStorageRootFromNodeConfig(const std::optional<std::string>& config_path) {
  std::optional<std::filesystem::path> resolved = config_path;
  if (!resolved.has_value()) {
    if (const char* env_path = std::getenv("NAIM_NODE_CONFIG_PATH");
        env_path != nullptr && *env_path != '\0') {
      resolved = env_path;
    }
  }
  if (!resolved.has_value() || !std::filesystem::exists(*resolved)) {
    return "/var/lib/naim";
  }
  std::ifstream input(*resolved);
  const auto parsed = nlohmann::json::parse(input, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object()) {
    return "/var/lib/naim";
  }
  if (parsed.contains("paths") && parsed.at("paths").is_object() &&
      parsed.at("paths").contains("storage_root") &&
      parsed.at("paths").at("storage_root").is_string()) {
    return parsed.at("paths").at("storage_root").get<std::string>();
  }
  if (parsed.contains("storage_root") && parsed.at("storage_root").is_string()) {
    return parsed.at("storage_root").get<std::string>();
  }
  return "/var/lib/naim";
}

}  // namespace

LauncherRunService::LauncherRunService(
    const InstallLayoutResolver& install_layout_resolver,
    const GeneratedConfigLoader& config_loader,
    const ProcessRunner& process_runner,
    const LauncherInstallService& install_service)
    : install_layout_resolver_(install_layout_resolver),
      config_loader_(config_loader),
      process_runner_(process_runner),
      install_service_(install_service) {}

int LauncherRunService::RunController(
    SignalManager& signal_manager,
    const std::filesystem::path& self_path,
    const std::filesystem::path& controller_binary,
    const LauncherCommandLine& command_line) const {
  ControllerRunOptions options;
  std::optional<GeneratedConfig> loaded_config;
  if (const auto config_path = config_loader_.ResolveConfigPathFromEnvOrDefault();
      config_path.has_value()) {
    loaded_config = config_loader_.Load(*config_path);
  }

  options.db_path =
      command_line.FindFlagValue("--db")
          .value_or(loaded_config && loaded_config->controller.db_path.has_value()
                        ? loaded_config->controller.db_path->string()
                        : install_layout_resolver_.DefaultControllerDbPath().string());
  options.artifacts_root =
      command_line.FindFlagValue("--artifacts-root")
          .value_or(loaded_config && loaded_config->controller.artifacts_root.has_value()
                        ? loaded_config->controller.artifacts_root->string()
                        : install_layout_resolver_.DefaultArtifactsRoot().string());
  options.web_ui_root =
      command_line.FindFlagValue("--web-ui-root")
          .value_or(install_layout_resolver_.DefaultWebUiRoot().string());
  options.listen_host =
      command_line.FindFlagValue("--listen-host")
          .value_or(loaded_config && loaded_config->controller.listen_host.has_value()
                        ? *loaded_config->controller.listen_host
                        : options.listen_host);
  options.listen_port = LauncherCommandLine::ParseIntValue(
      command_line.FindFlagValue("--listen-port"),
      loaded_config && loaded_config->controller.listen_port.has_value()
          ? *loaded_config->controller.listen_port
          : options.listen_port);
  options.internal_listen_host =
      command_line.FindFlagValue("--internal-listen-host")
          .value_or(loaded_config && loaded_config->controller.internal_listen_host.has_value()
                        ? *loaded_config->controller.internal_listen_host
                        : DefaultInternalListenHost());
  options.controller_upstream =
      command_line.FindFlagValue("--controller-upstream").value_or("");
  options.skills_factory_listen_port = LauncherCommandLine::ParseIntValue(
      command_line.FindFlagValue("--skills-factory-listen-port"),
      options.skills_factory_listen_port);
  options.compose_mode =
      command_line.FindFlagValue("--compose-mode")
          .value_or(loaded_config && loaded_config->hostd.compose_mode.has_value()
                        ? *loaded_config->hostd.compose_mode
                        : options.compose_mode);
  options.hostd_compose_mode =
      command_line.FindFlagValue("--hostd-compose-mode")
          .value_or(loaded_config && loaded_config->hostd.compose_mode.has_value()
                        ? *loaded_config->hostd.compose_mode
                        : options.hostd_compose_mode);
  options.node_name =
      command_line.FindFlagValue("--node")
          .value_or(loaded_config && loaded_config->hostd.node_name.has_value()
                        ? *loaded_config->hostd.node_name
                        : options.node_name);
  options.runtime_root =
      command_line.FindFlagValue("--runtime-root")
          .value_or(loaded_config && loaded_config->hostd.runtime_root.has_value()
                        ? loaded_config->hostd.runtime_root->string()
                        : install_layout_resolver_.DefaultRuntimeRoot().string());
  options.state_root =
      command_line.FindFlagValue("--state-root")
          .value_or(loaded_config && loaded_config->hostd.state_root.has_value()
                        ? loaded_config->hostd.state_root->string()
                        : install_layout_resolver_.DefaultHostdStateRoot().string());

  const bool config_local_hostd_enabled =
      loaded_config && loaded_config->controller.local_hostd_enabled.value_or(false);
  const bool managed_hostd_service_present =
      std::getenv("NAIM_SERVICE_MODE") != nullptr &&
      std::string(std::getenv("NAIM_SERVICE_MODE")) == "1" &&
      std::filesystem::exists(
          install_service_.ParseLayout(command_line).systemd_dir / "naim-node-hostd.service");
  options.with_hostd =
      (command_line.HasFlag("--with-hostd") ||
       (!command_line.HasFlag("--without-hostd") && config_local_hostd_enabled)) &&
      !managed_hostd_service_present;
  options.with_web_ui =
      command_line.HasFlag("--with-web-ui") ||
      (!command_line.HasFlag("--without-web-ui") &&
       loaded_config && loaded_config->controller.web_ui_enabled.value_or(false));
  options.hostd_poll_interval_sec = LauncherCommandLine::ParseIntValue(
      command_line.FindFlagValue("--poll-interval-sec"),
      options.hostd_poll_interval_sec);

  if (!(std::getenv("NAIM_SERVICE_MODE") != nullptr &&
        std::string(std::getenv("NAIM_SERVICE_MODE")) == "1") &&
      !command_line.HasFlag("--foreground") &&
      !command_line.HasFlag("--skip-systemctl")) {
    const auto layout = install_service_.ParseLayout(command_line);
    const auto default_layout = install_layout_resolver_.DefaultInstallLayout();
    if (process_runner_.CommandExists("systemctl")) {
      std::vector<std::string> install_args;
      if (layout.config_path != default_layout.config_path) {
        install_args.insert(install_args.end(), {"--config", layout.config_path.string()});
      }
      if (layout.state_root != default_layout.state_root) {
        install_args.insert(install_args.end(), {"--state-root", layout.state_root.string()});
      }
      if (layout.log_root != default_layout.log_root) {
        install_args.insert(install_args.end(), {"--log-root", layout.log_root.string()});
      }
      if (layout.systemd_dir != default_layout.systemd_dir) {
        install_args.insert(install_args.end(), {"--systemd-dir", layout.systemd_dir.string()});
      }
      install_args.insert(install_args.end(), {"--listen-host", options.listen_host});
      install_args.insert(
          install_args.end(), {"--listen-port", std::to_string(options.listen_port)});
      install_args.insert(
          install_args.end(), {"--internal-listen-host", options.internal_listen_host});
      install_args.insert(
          install_args.end(), {"--compose-mode", options.hostd_compose_mode});
      install_args.insert(install_args.end(), {"--node", options.node_name});
      if (options.with_hostd) {
        install_args.push_back("--with-hostd");
      }
      if (options.with_web_ui) {
        install_args.push_back("--with-web-ui");
      }
      install_service_.InstallController(self_path, LauncherCommandLine(std::move(install_args)));
      std::cout << "service_mode=systemd\n";
      std::cout << "controller_service=naim-node-controller.service\n";
      if (options.with_hostd) {
        std::cout << "hostd_service=naim-node-hostd.service\n";
      }
      return 0;
    }
  }

  return RunControllerSupervisor(signal_manager, self_path, controller_binary, options);
}

int LauncherRunService::RunHostd(
    SignalManager& signal_manager,
    const std::filesystem::path& hostd_binary,
    const std::filesystem::path& self_path,
    const LauncherCommandLine& command_line) const {
  HostdRunOptions options;
  options.db_path = command_line.FindFlagValue("--db").value_or("");

  std::optional<GeneratedConfig> loaded_config;
  if (const auto config_path = config_loader_.ResolveConfigPathFromEnvOrDefault();
      config_path.has_value()) {
    loaded_config = config_loader_.Load(*config_path);
  }

  options.controller_url =
      command_line.FindFlagValue("--controller")
          .value_or(loaded_config && loaded_config->hostd.controller_url.has_value()
                        ? *loaded_config->hostd.controller_url
                        : "");
  options.controller_fingerprint =
      command_line.FindFlagValue("--controller-fingerprint")
          .value_or(loaded_config &&
                            loaded_config->hostd.trusted_controller_fingerprint.has_value()
                        ? *loaded_config->hostd.trusted_controller_fingerprint
                        : "");
  options.onboarding_key =
      command_line.FindFlagValue("--onboarding-key")
          .value_or(loaded_config && loaded_config->hostd.onboarding_key.has_value()
                        ? *loaded_config->hostd.onboarding_key
                        : "");
  options.node_name =
      command_line.FindFlagValue("--node")
          .value_or(loaded_config && loaded_config->hostd.node_name.has_value()
                        ? *loaded_config->hostd.node_name
                        : DefaultNodeName());
  options.runtime_root =
      command_line.FindFlagValue("--runtime-root")
          .value_or(loaded_config && loaded_config->hostd.runtime_root.has_value()
                        ? loaded_config->hostd.runtime_root->string()
                        : install_layout_resolver_.DefaultRuntimeRoot().string());
  options.state_root =
      command_line.FindFlagValue("--state-root")
          .value_or(loaded_config && loaded_config->hostd.state_root.has_value()
                        ? loaded_config->hostd.state_root->string()
                        : install_layout_resolver_.DefaultHostdStateRoot().string());
  options.storage_root =
      LoadStorageRootFromNodeConfig(command_line.FindFlagValue("--config"));
  options.host_private_key_path =
      command_line.FindFlagValue("--host-private-key")
          .value_or(loaded_config && loaded_config->hostd.host_private_key.has_value()
                        ? loaded_config->hostd.host_private_key->string()
                        : "");
  options.compose_mode =
      command_line.FindFlagValue("--compose-mode").value_or(options.compose_mode);
  options.poll_interval_sec = LauncherCommandLine::ParseIntValue(
      command_line.FindFlagValue("--poll-interval-sec"), options.poll_interval_sec);
  options.inventory_scan_interval_sec = LauncherCommandLine::ParseIntValue(
      command_line.FindFlagValue("--inventory-scan-interval-sec"),
      loaded_config && loaded_config->hostd.inventory_scan_interval_sec.has_value()
          ? *loaded_config->hostd.inventory_scan_interval_sec
          : options.inventory_scan_interval_sec);

  if (!(std::getenv("NAIM_SERVICE_MODE") != nullptr &&
        std::string(std::getenv("NAIM_SERVICE_MODE")) == "1") &&
      !command_line.HasFlag("--foreground") &&
      !command_line.HasFlag("--skip-systemctl") &&
      process_runner_.CommandExists("systemctl")) {
    const auto layout = install_service_.ParseLayout(command_line);
    const auto default_layout = install_layout_resolver_.DefaultInstallLayout();
    std::vector<std::string> install_args;
    if (layout.config_path != default_layout.config_path) {
      install_args.insert(install_args.end(), {"--config", layout.config_path.string()});
    }
    if (layout.state_root != default_layout.state_root) {
      install_args.insert(install_args.end(), {"--state-root", layout.state_root.string()});
    }
    if (layout.log_root != default_layout.log_root) {
      install_args.insert(install_args.end(), {"--log-root", layout.log_root.string()});
    }
    if (layout.systemd_dir != default_layout.systemd_dir) {
      install_args.insert(install_args.end(), {"--systemd-dir", layout.systemd_dir.string()});
    }
    if (!options.controller_url.empty()) {
      install_args.insert(install_args.end(), {"--controller", options.controller_url});
    }
    if (!options.controller_fingerprint.empty()) {
      install_args.insert(
          install_args.end(), {"--controller-fingerprint", options.controller_fingerprint});
    }
    install_args.insert(install_args.end(), {"--node", options.node_name});
    install_args.insert(install_args.end(), {"--compose-mode", options.compose_mode});
    install_service_.InstallHostd(self_path, LauncherCommandLine(std::move(install_args)));
    std::cout << "service_mode=systemd\n";
    std::cout << "hostd_service=naim-node-hostd.service\n";
    return 0;
  }

  return RunHostdLoop(signal_manager, hostd_binary, options);
}

int LauncherRunService::RunHostdLoop(
    SignalManager& signal_manager,
    const std::filesystem::path& hostd_binary,
    const HostdRunOptions& options) const {
  if (options.controller_url.empty() && options.db_path.empty()) {
    throw std::runtime_error("--db is required for current hostd run mode");
  }

  std::cout << "hostd_node=" << options.node_name << "\n";
  if (!options.controller_url.empty()) {
    std::cout << "hostd_mode=remote\n";
    std::cout << "controller_url=" << options.controller_url << "\n";
  } else {
    std::cout << "hostd_mode=local-db\n";
    std::cout << "db_path=" << options.db_path << "\n";
  }
  std::cout
      << "next_step=leave hostd running so it can receive assignments and upload telemetry\n";
  constexpr int kTelemetryIntervalMs = 2000;
  constexpr int kTelemetryTtlMs = 10000;
  constexpr std::chrono::milliseconds kApplySessionHandoffGap(1500);
  auto next_inventory_report_at = std::chrono::steady_clock::now();
#if defined(_WIN32)
  auto next_telemetry_report_at = std::chrono::steady_clock::now();
#endif
  const auto self_update_marker =
      HostdSelfUpdateMarkerPath(options.state_root, options.node_name);
  std::error_code marker_error;
  std::filesystem::remove(self_update_marker, marker_error);
  HostdPeerService peer_service(options);
  peer_service.Start();
  if (peer_service.enabled()) {
    std::cout << "peer_discovery=enabled\n";
  }

  const auto build_apply_args = [&]() {
    std::vector<std::string> args = {
        hostd_binary.string(),
        "apply-next-assignment",
        "--node",
        options.node_name,
        "--runtime-root",
        options.runtime_root.string(),
        "--state-root",
        options.state_root.string(),
        "--compose-mode",
        options.compose_mode,
    };
    if (!options.controller_url.empty()) {
      args.insert(args.end(), {"--controller", options.controller_url});
      if (!options.controller_fingerprint.empty()) {
        args.insert(args.end(), {"--controller-fingerprint", options.controller_fingerprint});
      }
      if (!options.onboarding_key.empty()) {
        args.insert(args.end(), {"--onboarding-key", options.onboarding_key});
      }
      if (!options.host_private_key_path.empty()) {
        args.insert(args.end(), {"--host-private-key", options.host_private_key_path.string()});
      }
    } else {
      args.insert(args.end(), {"--db", options.db_path.string()});
    }
    return args;
  };

  const auto build_report_args = [&]() {
    std::vector<std::string> args = {
        hostd_binary.string(),
        "report-observed-state",
        "--node",
        options.node_name,
        "--state-root",
        options.state_root.string(),
    };
    if (!options.controller_url.empty()) {
      args.insert(args.end(), {"--controller", options.controller_url});
      if (!options.controller_fingerprint.empty()) {
        args.insert(args.end(), {"--controller-fingerprint", options.controller_fingerprint});
      }
      if (!options.onboarding_key.empty()) {
        args.insert(args.end(), {"--onboarding-key", options.onboarding_key});
      }
      if (!options.host_private_key_path.empty()) {
        args.insert(args.end(), {"--host-private-key", options.host_private_key_path.string()});
      }
    } else {
      args.insert(args.end(), {"--db", options.db_path.string()});
    }
    return args;
  };

  std::atomic_bool apply_session_owner_active{false};

#if defined(_WIN32)
  const auto build_telemetry_args = [&](const bool watch) {
    std::vector<std::string> args = {
        hostd_binary.string(),
        "report-telemetry",
        "--node",
        options.node_name,
        "--state-root",
        options.state_root.string(),
        "--interval-ms",
        std::to_string(kTelemetryIntervalMs),
        "--ttl-ms",
        std::to_string(kTelemetryTtlMs),
    };
    if (watch) {
      args.push_back("--watch");
    }
    if (!options.controller_url.empty()) {
      args.insert(args.end(), {"--controller", options.controller_url});
      if (!options.controller_fingerprint.empty()) {
        args.insert(args.end(), {"--controller-fingerprint", options.controller_fingerprint});
      }
      if (!options.onboarding_key.empty()) {
        args.insert(args.end(), {"--onboarding-key", options.onboarding_key});
      }
      if (!options.host_private_key_path.empty()) {
        args.insert(args.end(), {"--host-private-key", options.host_private_key_path.string()});
      }
    } else {
      args.insert(args.end(), {"--db", options.db_path.string()});
    }
    return args;
  };
#endif

  const auto run_report_if_due = [&]() {
    const auto now = std::chrono::steady_clock::now();
    if (now < next_inventory_report_at) {
      return;
    }
    if (!options.controller_url.empty() && apply_session_owner_active.load()) {
      next_inventory_report_at =
          now + std::chrono::seconds(std::max(1, options.inventory_scan_interval_sec));
      return;
    }
    const int report_code = process_runner_.RunCommand(build_report_args());
    if (report_code != 0) {
      std::cerr << "naim-node: hostd report-observed-state exit=" << report_code << "\n";
    }
    next_inventory_report_at =
        now + std::chrono::seconds(std::max(1, options.inventory_scan_interval_sec));
  };

#if defined(_WIN32)
  const auto run_telemetry_if_due = [&]() {
    const auto now = std::chrono::steady_clock::now();
    if (now < next_telemetry_report_at) {
      return;
    }
    const int telemetry_code = process_runner_.RunCommand(build_telemetry_args(false));
    if (telemetry_code != 0) {
      std::cerr << "naim-node: hostd report-telemetry exit=" << telemetry_code << "\n";
    }
    next_telemetry_report_at = now + std::chrono::milliseconds(kTelemetryIntervalMs);
  };
#else
  std::atomic_bool telemetry_runtime_stop{false};
  LauncherTelemetryBus telemetry_bus;
  LauncherTelemetryRuntimeMetrics telemetry_metrics;
  std::thread telemetry_collector_thread;
  std::thread telemetry_publisher_thread;
  const auto stop_telemetry_runtime = [&]() {
    telemetry_runtime_stop.store(true);
    telemetry_bus.Stop();
    if (telemetry_collector_thread.joinable()) {
      telemetry_collector_thread.join();
    }
    if (telemetry_publisher_thread.joinable()) {
      telemetry_publisher_thread.join();
    }
  };
  const auto start_telemetry_runtime = [&]() {
    telemetry_runtime_stop.store(false);
    telemetry_collector_thread = std::thread([&, telemetry_interval_ms = kTelemetryIntervalMs,
                                               telemetry_ttl_ms = kTelemetryTtlMs]() {
      auto interval = std::chrono::milliseconds(telemetry_interval_ms);
      auto slow_interval = std::max(
          interval * 6,
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::seconds(30)));
      std::cout << "hostd_telemetry_runtime=in-process\n";
      std::cout << "hostd_telemetry_bus=bounded-local\n";
      std::cout << "hostd_telemetry_initial_interval_ms=" << interval.count() << "\n";
      std::cout << "hostd_telemetry_initial_slow_interval_ms=" << slow_interval.count() << "\n";
      std::cout.flush();
      while (!telemetry_runtime_stop.load()) {
        try {
          naim::hostd::HostdReportingSupport reporting_support;
          auto next_tick = std::chrono::steady_clock::now();
          auto next_slow_tick = next_tick;
          while (!telemetry_runtime_stop.load()) {
            next_tick += interval;
            const auto now = std::chrono::steady_clock::now();
            const bool include_slow_lane = now >= next_slow_tick;
            if (include_slow_lane) {
              next_slow_tick = now + slow_interval;
            }
            const auto collect_started_at = std::chrono::steady_clock::now();
            auto frame = reporting_support.BuildTelemetryFrame(
                options.node_name,
                options.storage_root,
                options.state_root.string(),
                static_cast<int>(interval.count()),
                telemetry_ttl_ms,
                include_slow_lane);
            const auto collect_finished_at = std::chrono::steady_clock::now();
            const auto cadence = ResolveTelemetryCadence(frame);
            frame.collector_duration_ms = DurationMillis(collect_started_at, collect_finished_at);
            frame.publish_duration_ms = telemetry_metrics.last_publish_duration_ms.load();
            frame.telemetry_bus_depth = telemetry_bus.Depth();
            frame.telemetry_dropped_frames = telemetry_bus.DroppedFrames();
            frame.publish_error_count = telemetry_metrics.publish_error_count.load();
            frame.adaptive_interval_ms = static_cast<int>(cadence.interval.count());
            frame.adaptive_reason = cadence.reason;
            frame.last_publish_error = LoadLastPublishError(telemetry_metrics);
            telemetry_bus.Publish(std::move(frame));
            interval = cadence.interval;
            slow_interval = std::max(
                interval * 6,
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::seconds(30)));
            while (!telemetry_runtime_stop.load() &&
                   std::chrono::steady_clock::now() < next_tick) {
              std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (std::chrono::steady_clock::now() > next_tick + interval) {
              next_tick = std::chrono::steady_clock::now();
            }
          }
        } catch (const std::exception& error) {
          if (!telemetry_runtime_stop.load()) {
            std::cerr << "naim-node: telemetry collector warning: " << error.what() << "\n";
            std::this_thread::sleep_for(std::chrono::seconds(1));
          }
        }
      }
    });
    telemetry_publisher_thread = std::thread([&]() {
      while (!telemetry_runtime_stop.load()) {
        try {
          LauncherTelemetryBackendSupport backend_support;
          naim::hostd::HostdBackendFactory backend_factory(backend_support);
          auto backend = backend_factory.CreateBackend(
              options.db_path.empty() ? std::nullopt
                                      : std::optional<std::string>(options.db_path.string()),
              options.controller_url.empty() ? std::nullopt
                                             : std::optional<std::string>(options.controller_url),
              options.host_private_key_path.empty()
                  ? std::nullopt
                  : std::optional<std::string>(options.host_private_key_path.string()),
              options.controller_fingerprint.empty()
                  ? std::nullopt
                  : std::optional<std::string>(options.controller_fingerprint),
              options.onboarding_key.empty()
                  ? std::nullopt
                  : std::optional<std::string>(options.onboarding_key),
              options.node_name,
              options.storage_root);
          while (!telemetry_runtime_stop.load()) {
            auto item = telemetry_bus.WaitPop(telemetry_runtime_stop);
            if (!item.has_value()) {
              continue;
            }
            item->frame.publisher_queue_delay_ms =
                DurationMillis(item->enqueued_at, std::chrono::steady_clock::now());
            item->frame.telemetry_bus_depth = telemetry_bus.Depth();
            item->frame.telemetry_dropped_frames = telemetry_bus.DroppedFrames();
            item->frame.publish_error_count = telemetry_metrics.publish_error_count.load();
            item->frame.publish_duration_ms =
                telemetry_metrics.last_publish_duration_ms.load();
            item->frame.last_publish_error = LoadLastPublishError(telemetry_metrics);
            const auto publish_started_at = std::chrono::steady_clock::now();
            backend->UpsertHostTelemetry(item->frame);
            telemetry_metrics.last_publish_duration_ms.store(
                DurationMillis(publish_started_at, std::chrono::steady_clock::now()));
            StoreLastPublishError(&telemetry_metrics, "");
          }
        } catch (const std::exception& error) {
          if (!telemetry_runtime_stop.load()) {
            telemetry_metrics.publish_error_count.fetch_add(1);
            StoreLastPublishError(&telemetry_metrics, error.what());
            std::cerr << "naim-node: telemetry publisher warning: " << error.what() << "\n";
            std::this_thread::sleep_for(std::chrono::seconds(1));
          }
        }
      }
    });
  };
#endif

  const auto wait_for_self_update_recreate = [&]() -> int {
#if !defined(_WIN32)
    stop_telemetry_runtime();
#endif
    peer_service.Stop();
    for (int second = 0; second < 300 && !signal_manager.stop_requested(); ++second) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
  };

#if !defined(_WIN32)
  pid_t apply_pid = -1;
  auto next_apply_spawn_at = std::chrono::steady_clock::now();
  const auto reap_apply_if_finished = [&]() -> bool {
    if (apply_pid <= 0) {
      return false;
    }
    const std::optional<int> apply_code = PollChildExitCode(apply_pid);
    if (!apply_code.has_value()) {
      return false;
    }
    if (*apply_code != 0) {
      std::cerr << "naim-node: hostd apply-next-assignment exit=" << *apply_code << "\n";
    }
    apply_pid = -1;
    apply_session_owner_active.store(false);
    next_apply_spawn_at = std::chrono::steady_clock::now() + kApplySessionHandoffGap;
    if (std::filesystem::exists(self_update_marker)) {
      std::filesystem::remove(self_update_marker, marker_error);
      std::cout << "hostd_self_update_scheduled=waiting_for_container_recreate\n";
      return true;
    }
    return false;
  };
  start_telemetry_runtime();
#endif

  while (!signal_manager.stop_requested()) {
#if defined(_WIN32)
    const int apply_code = process_runner_.RunCommand(build_apply_args());
    if (apply_code != 0) {
      std::cerr << "naim-node: hostd apply-next-assignment exit=" << apply_code << "\n";
    }
    if (std::filesystem::exists(self_update_marker)) {
      std::filesystem::remove(self_update_marker, marker_error);
      std::cout << "hostd_self_update_scheduled=waiting_for_container_recreate\n";
      return wait_for_self_update_recreate();
    }
#else
    if (reap_apply_if_finished()) {
      return wait_for_self_update_recreate();
    }
    if (apply_pid <= 0 && std::chrono::steady_clock::now() >= next_apply_spawn_at) {
      apply_session_owner_active.store(true);
      apply_pid = static_cast<pid_t>(process_runner_.SpawnCommand(build_apply_args()));
      if (apply_pid <= 0) {
        apply_session_owner_active.store(false);
        next_apply_spawn_at = std::chrono::steady_clock::now() + kApplySessionHandoffGap;
      }
    }
#endif

    run_report_if_due();
#if defined(_WIN32)
    run_telemetry_if_due();
#endif

    for (int second = 0;
         second < options.poll_interval_sec && !signal_manager.stop_requested();
         ++second) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
#if !defined(_WIN32)
      if (reap_apply_if_finished()) {
        return wait_for_self_update_recreate();
      }
#endif
      run_report_if_due();
#if defined(_WIN32)
      run_telemetry_if_due();
#endif
    }
  }
#if !defined(_WIN32)
  if (apply_pid > 0) {
    StopChildProcess(apply_pid);
    apply_session_owner_active.store(false);
  }
  stop_telemetry_runtime();
#endif
  peer_service.Stop();
  return 0;
}

void LauncherRunService::PrepareControllerRuntime(
    const std::filesystem::path& owner_probe_path,
    const ControllerRunOptions& options) const {
  std::filesystem::create_directories(options.db_path.parent_path());
  std::filesystem::create_directories(options.artifacts_root);
  std::filesystem::create_directories(options.runtime_root);
  std::filesystem::create_directories(options.state_root);
  if (options.with_web_ui) {
    std::filesystem::create_directories(options.web_ui_root);
  }

  const auto keys_root = options.state_root.parent_path() / "keys";
  const auto ensure_keypair = [&](const std::filesystem::path& private_key_path,
                                  const std::filesystem::path& public_key_path) {
    if (std::filesystem::exists(private_key_path) &&
        std::filesystem::exists(public_key_path)) {
      return;
    }
    std::filesystem::create_directories(private_key_path.parent_path());
    const auto keypair = naim::GenerateSigningKeypair();
    std::ofstream priv(private_key_path);
    std::ofstream pub(public_key_path);
    priv << keypair.private_key_base64 << "\n";
    pub << keypair.public_key_base64 << "\n";
  };
  ensure_keypair(keys_root / "controller.key.b64", keys_root / "controller.pub.b64");
  if (options.with_hostd) {
    ensure_keypair(keys_root / "hostd.key.b64", keys_root / "hostd.pub.b64");
  }

  PrepareSharedStateAccess(owner_probe_path, options.db_path);

  naim::ControllerStore store(options.db_path.string());
  store.Initialize();
  PrepareSharedStateAccess(owner_probe_path, options.db_path);
}

int LauncherRunService::RunControllerSupervisor(
    SignalManager& signal_manager,
    const std::filesystem::path& self_path,
    const std::filesystem::path& controller_binary,
    const ControllerRunOptions& options) const {
  PrepareControllerRuntime(self_path, options);
  const std::string admin_dial_host =
      options.listen_host.empty() || options.listen_host == "0.0.0.0"
          ? "127.0.0.1"
          : options.listen_host;
  const std::string local_controller_url =
      "http://" + admin_dial_host + ":" + std::to_string(options.listen_port);
  const std::string internal_controller_url =
      "http://" + options.internal_listen_host + ":" + std::to_string(options.listen_port);
  const std::string local_skills_factory_url =
      "http://" + options.skills_factory_listen_host + ":" +
      std::to_string(options.skills_factory_listen_port);
  const std::string web_ui_controller_upstream =
      options.controller_upstream.empty()
          ? DefaultWebUiControllerUpstream(options.internal_listen_host, options.listen_port)
          : options.controller_upstream;
  const auto controller_public_key_path =
      options.state_root.parent_path() / "keys" / "controller.pub.b64";
  const std::string controller_fingerprint =
      std::filesystem::exists(controller_public_key_path)
          ? ComputePublicKeyFingerprint(controller_public_key_path)
          : "";

  if (!SetEnvVar("NAIM_CONTROLLER_ADMIN_UPSTREAM", local_controller_url) ||
      !SetEnvVar("NAIM_CONTROLLER_INTERNAL_HOST", options.internal_listen_host) ||
      !SetEnvVar("NAIM_CONTROLLER_INTERNAL_UPSTREAM", internal_controller_url) ||
      !SetEnvVar("NAIM_SKILLS_FACTORY_UPSTREAM", local_skills_factory_url) ||
      !SetEnvVar("NAIM_WEB_UI_ROOT", options.web_ui_root.string()) ||
      !SetEnvVar("NAIM_HOSTD_NODE_NAME", options.node_name)) {
    throw std::runtime_error("failed to export controller internal routing environment");
  }

  if (options.with_web_ui) {
    std::vector<std::string> ensure_args = {
        controller_binary.string(), "ensure-web-ui", "--db", options.db_path.string(),
        "--web-ui-root",           options.web_ui_root.string(),
        "--listen-port",           "18081",
        "--controller-upstream",   web_ui_controller_upstream,
        "--compose-mode",          options.compose_mode,
    };
    if (process_runner_.RunCommand(ensure_args) != 0) {
      throw std::runtime_error("failed to ensure naim-web-ui");
    }
  }

  const pid_t skills_factory_pid = static_cast<pid_t>(process_runner_.SpawnCommand({
      controller_binary.string(), "serve-skills-factory", "--db", options.db_path.string(),
      "--artifacts-root", options.artifacts_root.string(), "--listen-host",
      options.skills_factory_listen_host, "--listen-port",
      std::to_string(options.skills_factory_listen_port),
  }));
  signal_manager.TrackChild(static_cast<int>(skills_factory_pid));

  const pid_t internal_controller_pid = static_cast<pid_t>(process_runner_.SpawnCommand({
      controller_binary.string(), "serve", "--db", options.db_path.string(),
      "--artifacts-root", options.artifacts_root.string(), "--listen-host",
      options.internal_listen_host, "--listen-port", std::to_string(options.listen_port),
      "--skills-factory-upstream", local_skills_factory_url,
  }));
  signal_manager.TrackChild(static_cast<int>(internal_controller_pid));

  pid_t controller_pid = internal_controller_pid;
  if (options.internal_listen_host != options.listen_host) {
    controller_pid = static_cast<pid_t>(process_runner_.SpawnCommand({
        controller_binary.string(), "serve", "--db", options.db_path.string(),
        "--artifacts-root", options.artifacts_root.string(), "--listen-host",
        options.listen_host, "--listen-port", std::to_string(options.listen_port),
        "--skills-factory-upstream", local_skills_factory_url,
    }));
    signal_manager.TrackChild(static_cast<int>(controller_pid));
  }

  pid_t hostd_pid = -1;
  if (options.with_hostd) {
    naim::ControllerStore store(options.db_path.string());
    store.Initialize();
    naim::RegisteredHostRecord host;
    if (const auto current = store.LoadRegisteredHost(options.node_name);
        current.has_value()) {
      host = *current;
    }
    host.node_name = options.node_name;
    host.advertised_address = local_controller_url;
    host.public_key_base64 =
        Trim(ReadTextFile(options.state_root.parent_path() / "keys" / "hostd.pub.b64"));
    host.controller_public_key_fingerprint = controller_fingerprint;
    host.transport_mode = "out";
    host.execution_mode = "mixed";
    host.registration_state = "registered";
    host.session_state = "disconnected";
    host.status_message = "auto-registered local hostd by naim-node run controller";
    store.UpsertRegisteredHost(host);

    hostd_pid = static_cast<pid_t>(process_runner_.SpawnCommand({
        self_path.string(), "run", "hostd", "--controller", local_controller_url,
        "--controller-fingerprint", controller_fingerprint, "--node", options.node_name,
        "--runtime-root", options.runtime_root.string(), "--state-root",
        options.state_root.string(), "--host-private-key",
        (options.state_root.parent_path() / "keys" / "hostd.key.b64").string(),
        "--foreground",
        "--compose-mode", options.hostd_compose_mode, "--poll-interval-sec",
        std::to_string(options.hostd_poll_interval_sec),
    }));
    signal_manager.TrackChild(static_cast<int>(hostd_pid));
  }

  std::cout << "controller_api_url=" << local_controller_url << "\n";
  std::cout << "controller_internal_url=" << internal_controller_url << "\n";
  std::cout << "skills_factory_url=" << local_skills_factory_url << "\n";
  if (options.with_web_ui) {
    std::cout << "web_ui_url=http://127.0.0.1:18081\n";
    std::cout << "next_step=open the Web UI and load a plane\n";
  } else {
    std::cout << "next_step=use controller API or CLI to load a plane\n";
  }

  while (!signal_manager.stop_requested()) {
    int status = 0;
    const auto exited = signal_manager.WaitForAnyChildProcess(&status);
    if (!exited.has_value()) {
      break;
    }
    signal_manager.RemoveChild(*exited);
    if (*exited == static_cast<int>(controller_pid) ||
        *exited == static_cast<int>(internal_controller_pid) ||
        *exited == static_cast<int>(skills_factory_pid) ||
        *exited == static_cast<int>(hostd_pid)) {
      signal_manager.RequestStop();
      break;
    }
  }

  if (controller_pid > 0) {
    signal_manager.TerminateChildProcess(static_cast<int>(controller_pid));
  }
  if (internal_controller_pid > 0 &&
      internal_controller_pid != controller_pid) {
    signal_manager.TerminateChildProcess(static_cast<int>(internal_controller_pid));
  }
  if (skills_factory_pid > 0) {
    signal_manager.TerminateChildProcess(static_cast<int>(skills_factory_pid));
  }
  if (hostd_pid > 0) {
    signal_manager.TerminateChildProcess(static_cast<int>(hostd_pid));
  }
  return 0;
}

void LauncherRunService::PrepareSharedStateAccess(
    const std::filesystem::path& owner_probe_path,
    const std::filesystem::path& db_path) const {
#if defined(_WIN32)
  (void)owner_probe_path;
  (void)db_path;
#else
  if (!naim::platform::HasElevatedPrivileges()) {
    return;
  }

  const auto group_id = ResolveSharedStateGroupId(owner_probe_path);
  if (!group_id.has_value()) {
    return;
  }

  try {
    EnsureSharedDirectoryAccess(db_path.parent_path(), *group_id);
    EnsureSharedFileAccess(db_path, *group_id);
    EnsureSharedFileAccess(db_path.string() + "-wal", *group_id);
    EnsureSharedFileAccess(db_path.string() + "-shm", *group_id);
    ::umask(0002);
  } catch (const std::exception& error) {
    std::cerr << "naim-node: warning: failed to prepare shared controller DB access: "
              << error.what() << "\n";
  }
#endif
}

std::optional<unsigned int> LauncherRunService::ResolveSharedStateGroupId(
    const std::filesystem::path& owner_probe_path) const {
#if defined(_WIN32)
  (void)owner_probe_path;
  return std::nullopt;
#else
  struct stat metadata {};
  if (::stat(owner_probe_path.c_str(), &metadata) != 0) {
    return std::nullopt;
  }
  return static_cast<unsigned int>(metadata.st_gid);
#endif
}

void LauncherRunService::EnsureSharedDirectoryAccess(
    const std::filesystem::path& path,
    unsigned int group_id) const {
#if defined(_WIN32)
  (void)path;
  (void)group_id;
#else
  if (path.empty() || !std::filesystem::exists(path)) {
    return;
  }

  struct stat metadata {};
  if (::stat(path.c_str(), &metadata) != 0) {
    throw std::runtime_error("stat failed for '" + path.string() + "'");
  }
  if (::chown(path.c_str(), metadata.st_uid, static_cast<gid_t>(group_id)) != 0) {
    throw std::runtime_error("chown failed for '" + path.string() + "'");
  }

  constexpr mode_t kSharedDirectoryMode = 02775;
  if (::chmod(path.c_str(), kSharedDirectoryMode) != 0) {
    throw std::runtime_error("chmod failed for '" + path.string() + "'");
  }
#endif
}

void LauncherRunService::EnsureSharedFileAccess(
    const std::filesystem::path& path,
    unsigned int group_id) const {
#if defined(_WIN32)
  (void)path;
  (void)group_id;
#else
  if (path.empty() || !std::filesystem::exists(path)) {
    return;
  }

  struct stat metadata {};
  if (::stat(path.c_str(), &metadata) != 0) {
    throw std::runtime_error("stat failed for '" + path.string() + "'");
  }
  if (::chown(path.c_str(), metadata.st_uid, static_cast<gid_t>(group_id)) != 0) {
    throw std::runtime_error("chown failed for '" + path.string() + "'");
  }

  constexpr mode_t kSharedFileMode = 0664;
  if (::chmod(path.c_str(), kSharedFileMode) != 0) {
    throw std::runtime_error("chmod failed for '" + path.string() + "'");
  }
#endif
}

std::string LauncherRunService::DefaultNodeName() const {
  return "local-hostd";
}

std::string LauncherRunService::DefaultInternalListenHost() const {
  const std::string route_probe =
      Trim(process_runner_.CaptureShellOutput(
          "sh -c \"ip -4 route get 1.1.1.1 2>/dev/null | sed -n 's/.* src \\([0-9.]*\\).*/\\1/p'\""));
  if (IsPrivateIpv4Address(route_probe)) {
    return route_probe;
  }

  const std::string host_ips =
      Trim(process_runner_.CaptureShellOutput("hostname -I 2>/dev/null"));
  if (!host_ips.empty()) {
    std::istringstream input(host_ips);
    std::string host_ip;
    while (input >> host_ip) {
      if (IsPrivateIpv4Address(host_ip)) {
        return host_ip;
      }
    }
    input.clear();
    input.str(host_ips);
    while (input >> host_ip) {
      if (IsIpv4Address(host_ip) && host_ip != "127.0.0.1") {
        return host_ip;
      }
    }
  }
  return "127.0.0.1";
}

std::string LauncherRunService::DefaultWebUiControllerUpstream(
    const std::string& internal_listen_host,
    int listen_port) const {
  if (!internal_listen_host.empty()) {
    return "http://" + internal_listen_host + ":" + std::to_string(listen_port);
  }
  return "http://host.docker.internal:" + std::to_string(listen_port);
}

std::string LauncherRunService::Trim(const std::string& value) const {
  std::size_t begin = 0;
  while (begin < value.size() &&
         std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }
  std::size_t end = value.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }
  return value.substr(begin, end - begin);
}

std::string LauncherRunService::ReadTextFile(
    const std::filesystem::path& path) const {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("failed to read file '" + path.string() + "'");
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::string LauncherRunService::ComputePublicKeyFingerprint(
    const std::filesystem::path& public_key_path) const {
  return naim::ComputeKeyFingerprintHex(Trim(ReadTextFile(public_key_path)));
}

}  // namespace naim::launcher
