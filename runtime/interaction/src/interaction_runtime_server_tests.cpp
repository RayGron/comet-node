#include <chrono>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "http/controller_http_transport.h"
#include "interaction/interaction_types.h"
#include "naim/core/platform_compat.h"

#define private public
#include "interaction/interaction_runtime_server.h"
#undef private

#include "interaction/interaction_completion_policy_support.h"
#include "interaction/interaction_runtime_request_codec.h"
#include "naim/state/state_json.h"

namespace {

void Expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class TempDir final {
 public:
  TempDir() {
    path_ = std::filesystem::temp_directory_path() /
            ("naim-interaction-runtime-tests-" + std::to_string(getpid()) + "-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(path_);
  }

  ~TempDir() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::filesystem::path& path() const {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

class SsePathCaptureServer final {
 public:
  SsePathCaptureServer() {
    naim::platform::EnsureSocketsInitialized();
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (!naim::platform::IsSocketValid(listen_fd_)) {
      throw std::runtime_error("failed to create SSE capture server socket");
    }

    int yes = 1;
#if defined(_WIN32)
    setsockopt(
        listen_fd_,
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char*>(&yes),
        sizeof(yes));
#else
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
      const auto error = naim::platform::LastSocketErrorMessage();
      naim::platform::CloseSocket(listen_fd_);
      throw std::runtime_error("failed to bind SSE capture server: " + error);
    }
    if (listen(listen_fd_, 1) != 0) {
      const auto error = naim::platform::LastSocketErrorMessage();
      naim::platform::CloseSocket(listen_fd_);
      throw std::runtime_error("failed to listen SSE capture server: " + error);
    }

    sockaddr_in bound_addr{};
    socklen_t bound_size = sizeof(bound_addr);
    if (getsockname(
            listen_fd_,
            reinterpret_cast<sockaddr*>(&bound_addr),
            &bound_size) != 0) {
      const auto error = naim::platform::LastSocketErrorMessage();
      naim::platform::CloseSocket(listen_fd_);
      throw std::runtime_error("failed to inspect SSE capture server: " + error);
    }
    port_ = ntohs(bound_addr.sin_port);
    thread_ = std::thread([this]() { Serve(); });
  }

  ~SsePathCaptureServer() {
    stop_requested_.store(true);
    if (port_ > 0) {
      const auto wake_fd = socket(AF_INET, SOCK_STREAM, 0);
      if (naim::platform::IsSocketValid(wake_fd)) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port_));
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        (void)connect(wake_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        naim::platform::CloseSocket(wake_fd);
      }
    }
    if (naim::platform::IsSocketValid(listen_fd_)) {
      naim::platform::CloseSocket(listen_fd_);
    }
    Wait();
  }

  int port() const { return port_; }
  void Wait() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }
  std::string request_line() const { return request_line_; }

 private:
  void Serve() {
    sockaddr_in client_addr{};
    socklen_t client_size = sizeof(client_addr);
    const auto client_fd =
        accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_size);
    if (!naim::platform::IsSocketValid(client_fd)) {
      return;
    }
    if (stop_requested_.load()) {
      naim::platform::CloseSocket(client_fd);
      return;
    }

    char buffer[4096];
    const auto read_count = recv(client_fd, buffer, sizeof(buffer), 0);
    const std::string request =
        read_count > 0 ? std::string(buffer, static_cast<std::size_t>(read_count)) : "";
    const std::size_t line_end = request.find("\r\n");
    request_line_ = request.substr(0, line_end == std::string::npos ? request.size() : line_end);

    const std::string body =
        "data: {\"choices\":[{\"delta\":{\"content\":\"OK\"}}]}\n\n"
        "data: [DONE]\n\n";
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: text/event-stream\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Connection: close\r\n\r\n";
    response << body;
    const std::string serialized = response.str();
    const char* data = serialized.c_str();
    std::size_t remaining = serialized.size();
    while (remaining > 0) {
      const auto written = send(client_fd, data, remaining, 0);
      if (written <= 0) {
        break;
      }
      data += written;
      remaining -= static_cast<std::size_t>(written);
    }
    naim::platform::CloseSocket(client_fd);
  }

  naim::platform::SocketHandle listen_fd_ = naim::platform::kInvalidSocket;
  int port_ = 0;
  std::atomic<bool> stop_requested_{false};
  std::thread thread_;
  std::string request_line_;
};

naim::DesiredState BuildDesiredState() {
  naim::DesiredState state;
  state.plane_name = "test-plane";
  state.control_root = "/naim/shared/control/test-plane";
  state.plane_mode = naim::PlaneMode::Llm;
  state.interaction = naim::InteractionSettings{};
  state.interaction->system_prompt = "You are a test assistant.";
  state.skills = naim::SkillsSettings{};
  state.skills->enabled = true;
  naim::BrowsingSettings browsing;
  browsing.enabled = true;
  naim::BrowsingPolicySettings browsing_policy;
  browsing_policy.browser_session_enabled = true;
  browsing_policy.rendered_browser_enabled = true;
  browsing.policy = browsing_policy;
  state.browsing = browsing;

  naim::InstanceSpec skills;
  skills.name = "skills-test-plane";
  skills.role = naim::InstanceRole::Skills;
  skills.plane_name = state.plane_name;
  skills.node_name = "hpc1";
  skills.environment["NAIM_SKILLS_PORT"] = "18120";
  skills.published_ports.push_back(naim::PublishedPort{"127.0.0.1", 26182, 18120});
  state.instances.push_back(skills);

  naim::InstanceSpec webgateway;
  webgateway.name = "webgateway-test-plane";
  webgateway.role = naim::InstanceRole::Browsing;
  webgateway.plane_name = state.plane_name;
  webgateway.node_name = "hpc1";
  webgateway.environment["NAIM_WEBGATEWAY_PORT"] = "18130";
  webgateway.published_ports.push_back(naim::PublishedPort{"127.0.0.1", 26183, 18130});
  state.instances.push_back(webgateway);
  return state;
}

void WriteSnapshot(const std::filesystem::path& root, const naim::DesiredState& state) {
  std::filesystem::create_directories(root);
  std::ofstream output(root / "desired-state.v2.json", std::ios::binary | std::ios::trunc);
  output << naim::SerializeDesiredStateJson(state);
}

naim::interaction_runtime::InteractionRuntimeServer BuildServer(
    const std::filesystem::path& control_root) {
  naim::interaction_runtime::InteractionRuntimeConfig config;
  config.plane_name = "test-plane";
  config.control_root = control_root.string();
  config.controller_url = "http://controller.internal:18080";
  config.upstream_base = "http://infer-test-plane:18084/v1";
  return naim::interaction_runtime::InteractionRuntimeServer(std::move(config));
}

HttpRequest RawChatRequest(const std::string& body) {
  HttpRequest request;
  request.method = "POST";
  request.path = "/v1/chat/completions";
  request.body = body;
  return request;
}

void TestRawRequestUsesLocalSnapshot() {
  TempDir temp;
  WriteSnapshot(temp.path(), BuildDesiredState());
  auto server = BuildServer(temp.path());
  const HttpRequest request = RawChatRequest(
      R"({"messages":[{"role":"user","content":"Answer OK"}],"auto_skills":true})");

  Expect(!server.ShouldProxyRawRequestThroughController(request),
         "raw request with local snapshot should not proxy through controller");
  const auto execution = server.BuildDirectRuntimeExecution(request);
  Expect(execution.local_raw_execution, "direct execution should mark local raw path");
  Expect(execution.skills_resolve_ms == 0,
         "default auto_skills without skill_ids should not resolve skills");
  Expect(
      execution.request_context.payload.value(
          "_naim_skill_resolution_mode", std::string{}) == "none",
      "default auto_skills should be marked as no resolved skills");
}

void TestRawRequestFallsBackWithoutSnapshot() {
  TempDir temp;
  auto server = BuildServer(temp.path());
  const HttpRequest request = RawChatRequest(
      R"({"messages":[{"role":"user","content":"Answer OK"}]})");
  Expect(server.ShouldProxyRawRequestThroughController(request),
         "raw request without local snapshot should fall back to controller");
}

void TestWrappedRequestNeverUsesRawProxy() {
  auto state = BuildDesiredState();
  naim::controller::InteractionRuntimeExecutionRequest wrapped;
  wrapped.desired_state = state;
  wrapped.status_payload = nlohmann::json{{"plane_name", state.plane_name}};
  wrapped.payload = nlohmann::json{
      {"messages",
       nlohmann::json::array(
           {nlohmann::json{{"role", "user"}, {"content", "Answer OK"}}})}};
  wrapped.resolved_policy =
      naim::controller::InteractionCompletionPolicySupport{}.ResolvePolicy(
          state,
          wrapped.payload);

  TempDir temp;
  auto server = BuildServer(temp.path());
  const HttpRequest request = RawChatRequest(
      naim::controller::InteractionRuntimeRequestCodec{}.Serialize(wrapped));
  Expect(!server.ShouldProxyRawRequestThroughController(request),
         "wrapped runtime request should never use raw controller proxy");
}

void TestPlaneNetworkSkillsTargetUsesContainerPort() {
  TempDir temp;
  auto server = BuildServer(temp.path());
  const auto target = server.ResolvePlaneNetworkSkillsTarget(BuildDesiredState());
  Expect(target.has_value(), "skills plane-network target should resolve");
  Expect(target->host == "skills-test-plane", "skills target should use instance DNS name");
  Expect(target->port == 18120, "skills target should use container port");
  Expect(!target->route_via_hostd_proxy, "plane-network skills target should not use hostd proxy");
}

void TestPlaneNetworkWebGatewayTargetUsesPlaneDns() {
  TempDir temp;
  auto server = BuildServer(temp.path());
  const auto target = server.ResolvePlaneNetworkWebGatewayTarget(BuildDesiredState());
  Expect(target.has_value(), "webgateway plane-network target should resolve");
  Expect(target->host == "webgateway-test-plane",
         "webgateway target should use instance DNS name");
  Expect(target->port == 18130, "webgateway target should use container port");
  Expect(target->base_path == "/v1/webgateway",
         "webgateway target should use runtime API base path");
  Expect(!target->route_via_hostd_proxy,
         "plane-network webgateway target should not use hostd proxy");
}

void TestPlaneOwnedBrowsingDisabledDoesNotUseController() {
  TempDir temp;
  auto state = BuildDesiredState();
  state.browsing->enabled = false;
  auto server = BuildServer(temp.path());
  naim::controller::PlaneInteractionResolution resolution;
  resolution.desired_state = state;
  naim::controller::InteractionRequestContext context;
  context.payload = nlohmann::json{
      {"messages",
       nlohmann::json::array(
           {nlohmann::json{{"role", "user"}, {"content", "hello"}}})}};
  const int elapsed = server.ResolvePlaneOwnedBrowsing(resolution, &context);
  Expect(elapsed == 0, "disabled webgateway should not perform a runtime lookup");
  Expect(context.payload.contains("_naim_webgateway_context"),
         "disabled webgateway should still expose context");
  Expect(context.payload.at("_naim_webgateway_context").value("mode", std::string{}) ==
             "disabled",
         "disabled webgateway context should mark disabled mode");
}

void TestStreamRequestRespectsTargetBasePath() {
  SsePathCaptureServer server;
  const auto target = ParseControllerEndpointTarget(
      "http://127.0.0.1:" + std::to_string(server.port()) + "/v1");
  auto upstream = OpenInteractionStreamRequest(
      target,
      "interaction-runtime-test",
      R"({"messages":[],"stream":true})",
      "/chat/completions");
  upstream.close();
  server.Wait();
  Expect(
      server.request_line() == "POST /v1/chat/completions HTTP/1.1",
      "stream upstream path should not duplicate /v1 base path");
}

}  // namespace

int main() {
  try {
    TestRawRequestUsesLocalSnapshot();
    TestRawRequestFallsBackWithoutSnapshot();
    TestWrappedRequestNeverUsesRawProxy();
    TestPlaneNetworkSkillsTargetUsesContainerPort();
    TestPlaneNetworkWebGatewayTargetUsesPlaneDns();
    TestPlaneOwnedBrowsingDisabledDoesNotUseController();
    TestStreamRequestRespectsTargetBasePath();
    std::cout << "ok: interaction-runtime-server-fast-path-tests\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "interaction_runtime_server_tests failed: " << error.what() << '\n';
    return 1;
  }
}
