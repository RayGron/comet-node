#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
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

naim::DesiredState BuildDesiredState() {
  naim::DesiredState state;
  state.plane_name = "test-plane";
  state.control_root = "/naim/shared/control/test-plane";
  state.plane_mode = naim::PlaneMode::Llm;
  state.interaction = naim::InteractionSettings{};
  state.interaction->system_prompt = "You are a test assistant.";
  state.skills = naim::SkillsSettings{};
  state.skills->enabled = true;

  naim::InstanceSpec skills;
  skills.name = "skills-test-plane";
  skills.role = naim::InstanceRole::Skills;
  skills.plane_name = state.plane_name;
  skills.node_name = "hpc1";
  skills.environment["NAIM_SKILLS_PORT"] = "18120";
  skills.published_ports.push_back(naim::PublishedPort{"127.0.0.1", 26182, 18120});
  state.instances.push_back(skills);
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

}  // namespace

int main() {
  try {
    TestRawRequestUsesLocalSnapshot();
    TestRawRequestFallsBackWithoutSnapshot();
    TestWrappedRequestNeverUsesRawProxy();
    TestPlaneNetworkSkillsTargetUsesContainerPort();
    std::cout << "ok: interaction-runtime-server-fast-path-tests\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "interaction_runtime_server_tests failed: " << error.what() << '\n';
    return 1;
  }
}
