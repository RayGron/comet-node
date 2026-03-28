#include "app/launcher_app.h"

#include "cli/launcher_command_line.h"
#include "config/generated_config_loader.h"
#include "config/install_layout.h"
#include "install/launcher_install_service.h"
#include "platform/process_runner.h"
#include "platform/signal_manager.h"
#include "run/launcher_run_service.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "comet/core/platform_compat.h"
#include "comet/state/sqlite_store.h"

namespace {

namespace fs = std::filesystem;
using LauncherCommandLine = comet::launcher::LauncherCommandLine;
using GeneratedConfigLoader = comet::launcher::GeneratedConfigLoader;
using InstallLayoutResolver = comet::launcher::InstallLayoutResolver;
using LauncherInstallService = comet::launcher::LauncherInstallService;
using LauncherRunService = comet::launcher::LauncherRunService;
using ProcessRunner = comet::launcher::ProcessRunner;
using SignalManager = comet::launcher::SignalManager;

std::string Trim(const std::string& value) {
  std::size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }
  std::size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }
  return value.substr(begin, end - begin);
}

bool RunningInManagedServiceMode() {
  const char* value = std::getenv("COMET_SERVICE_MODE");
  return value != nullptr && std::string(value) == "1";
}

bool SystemdAvailable(const ProcessRunner& process_runner) {
#if defined(_WIN32)
  (void)process_runner;
  return false;
#else
  if (!process_runner.CommandExists("systemctl")) {
    return false;
  }
  if (comet::platform::HasElevatedPrivileges()) {
    return process_runner.RunShellCommand("systemctl is-system-running >/dev/null 2>&1") == 0;
  }
  return process_runner.RunShellCommand("systemctl --user is-system-running >/dev/null 2>&1") ==
         0;
#endif
}

std::string ReadTextFile(const fs::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("failed to read file '" + path.string() + "'");
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

fs::path ResolveSelfPath(const char* argv0) {
  const std::string executable_path = comet::platform::ExecutablePath();
  if (!executable_path.empty()) {
    return fs::path(executable_path);
  }
  return fs::weakly_canonical(fs::path(argv0));
}

fs::path ResolveSiblingBinary(const fs::path& self_path, const std::string& binary_name) {
  const fs::path sibling = self_path.parent_path() / binary_name;
  if (!fs::exists(sibling)) {
    throw std::runtime_error("required binary not found: " + sibling.string());
  }
  return sibling;
}

std::string ReadPublicKeyBase64Argument(const std::string& value) {
  const fs::path candidate(value);
  if (fs::exists(candidate)) {
    return Trim(ReadTextFile(candidate));
  }
  return value;
}

void RunDoctor(const fs::path& self_path, const std::optional<std::string>& role) {
  const ProcessRunner process_runner;
  const std::set<std::string> required_commands = {"docker"};
  const fs::path controller_binary = self_path.parent_path() / "comet-controller";
  const fs::path hostd_binary = self_path.parent_path() / "comet-hostd";
  std::cout << "doctor\n";
  std::cout << "binary=" << self_path << "\n";
  if (!role.has_value() || *role == "controller") {
    std::cout << "controller_binary=" << (fs::exists(controller_binary) ? "yes" : "no") << "\n";
  }
  if (!role.has_value() || *role == "hostd") {
    std::cout << "hostd_binary=" << (fs::exists(hostd_binary) ? "yes" : "no") << "\n";
  }
  for (const std::string& command : required_commands) {
    std::cout << command << "=" << (process_runner.CommandExists(command) ? "yes" : "no")
              << "\n";
  }
  std::cout << "systemctl=" << (process_runner.CommandExists("systemctl") ? "yes" : "no")
            << "\n";
  std::cout << "systemd_analyze="
            << (process_runner.CommandExists("systemd-analyze") ? "yes" : "no") << "\n";
}

void ConnectHostd(const LauncherCommandLine& command_line) {
  const auto db_path = command_line.FindFlagValue("--db");
  const auto node_name = command_line.FindFlagValue("--node");
  const auto public_key = command_line.FindFlagValue("--public-key");
  if (!db_path.has_value() || !node_name.has_value() || !public_key.has_value()) {
    throw std::runtime_error("--db, --node and --public-key are required for connect-hostd");
  }

  comet::ControllerStore store(*db_path);
  store.Initialize();
  comet::RegisteredHostRecord record;
  record.node_name = *node_name;
  record.advertised_address = command_line.FindFlagValue("--address").value_or("");
  record.public_key_base64 = ReadPublicKeyBase64Argument(*public_key);
  record.controller_public_key_fingerprint =
      command_line.FindFlagValue("--controller-fingerprint").value_or("");
  record.transport_mode = command_line.FindFlagValue("--transport").value_or("out");
  record.execution_mode = command_line.FindFlagValue("--execution-mode").value_or("mixed");
  record.registration_state = "registered";
  record.session_state = "disconnected";
  record.capabilities_json = "{}";
  record.status_message = "registered by comet-node connect-hostd";
  store.UpsertRegisteredHost(record);
  store.AppendEvent(comet::EventRecord{
      0,
      "",
      *node_name,
      "",
      std::nullopt,
      std::nullopt,
      "host-registry",
      "registered",
      "info",
      "registered hostd node",
      "{\"source\":\"comet-node connect-hostd\"}",
      "",
  });
  std::cout << "registered hostd node=" << *node_name << "\n";
}

}  // namespace

namespace comet::launcher {

int RunLauncherApp(int argc, char** argv) {
  SignalManager signal_manager;
  signal_manager.RegisterHandlers();

  const fs::path self_path = ResolveSelfPath(argv[0]);
  const LauncherCommandLine command_line = LauncherCommandLine::FromArgv(argc, argv);
  const InstallLayoutResolver install_layout_resolver;
  const ProcessRunner process_runner;
  const GeneratedConfigLoader config_loader(install_layout_resolver);
  const LauncherInstallService install_service(install_layout_resolver, process_runner);
  const LauncherRunService run_service(
      install_layout_resolver,
      config_loader,
      process_runner,
      install_service);

  if (!command_line.HasCommand()) {
    command_line.PrintUsage(std::cout);
    return 1;
  }

  const std::vector<std::string>& args = command_line.args();
  const std::string& command = command_line.command();

  try {
    if (command == "version") {
      std::cout << "comet-node 0.1.0\n";
      return 0;
    }

    if (command == "doctor") {
      RunDoctor(self_path, args.size() > 1 ? std::optional<std::string>(args[1]) : std::nullopt);
      return 0;
    }

    if (command == "connect-hostd") {
      ConnectHostd(command_line);
      return 0;
    }

    if (command == "install") {
      if (args.size() < 2) {
        throw std::runtime_error("install requires role");
      }
      const LauncherCommandLine install_command_line(command_line.Tail(2));
      if (args[1] == "controller") {
        install_service.InstallController(self_path, install_command_line);
        return 0;
      }
      if (args[1] == "hostd") {
        install_service.InstallHostd(self_path, install_command_line);
        return 0;
      }
      throw std::runtime_error("unknown install role '" + args[1] + "'");
    }

    if (command == "service") {
      if (args.size() < 3) {
        throw std::runtime_error("service requires action and role");
      }
      install_service.ServiceCommand(args[1], args[2], LauncherCommandLine(command_line.Tail(3)));
      return 0;
    }

    if (command == "run") {
      if (args.size() < 2) {
        throw std::runtime_error("run requires role");
      }
      if (args[1] == "controller") {
        if (!RunningInManagedServiceMode() &&
            !command_line.HasFlag("--foreground") &&
            !command_line.HasFlag("--skip-systemctl") &&
            SystemdAvailable(process_runner)) {
          return run_service.RunController(
              signal_manager,
              self_path,
              ResolveSiblingBinary(self_path, "comet-controller"),
              command_line);
        }
        return run_service.RunController(
            signal_manager,
            self_path,
            ResolveSiblingBinary(self_path, "comet-controller"),
            command_line);
      }

      if (args[1] == "hostd") {
        if (!RunningInManagedServiceMode() &&
            !command_line.HasFlag("--foreground") &&
            !command_line.HasFlag("--skip-systemctl") &&
            SystemdAvailable(process_runner)) {
          return run_service.RunHostd(
              signal_manager,
              ResolveSiblingBinary(self_path, "comet-hostd"),
              self_path,
              command_line);
        }
        return run_service.RunHostd(
            signal_manager,
            ResolveSiblingBinary(self_path, "comet-hostd"),
            self_path,
            command_line);
      }

      throw std::runtime_error("unknown run role '" + args[1] + "'");
    }
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }

  command_line.PrintUsage(std::cout);
  return 1;
}

}  // namespace comet::launcher
