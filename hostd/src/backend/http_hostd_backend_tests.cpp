#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "backend/http_hostd_backend.h"
#include "naim/security/crypto_utils.h"

namespace {

void Expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class FakeHttpHostdBackendSupport final : public naim::hostd::IHttpHostdBackendSupport {
 public:
  nlohmann::json SendControllerJsonRequest(
      const std::string&,
      const std::string& method,
      const std::string& path,
      const nlohmann::json&,
      const std::map<std::string, std::string>& headers = {}) const override {
    requests.push_back(Request{method, path, headers});
    if (path == "/api/v1/hostd/session/open") {
      return nlohmann::json{
          {"service", "naim-controller"},
          {"node_name", "hpc1"},
          {"session_state", "connected"},
          {"session_token", session_token},
          {"controller_sequence", 0},
      };
    }
    return nlohmann::json{{"status", "ok"}};
  }

  naim::HostAssignment ParseAssignmentPayload(const nlohmann::json&) const override {
    return {};
  }

  nlohmann::json BuildHostObservationPayload(const naim::HostObservation&) const override {
    return nlohmann::json::object();
  }

  nlohmann::json BuildHostTelemetryPayload(const naim::HostTelemetryFrame&) const override {
    return nlohmann::json::object();
  }

  nlohmann::json BuildDiskRuntimeStatePayload(const naim::DiskRuntimeState&) const override {
    return nlohmann::json::object();
  }

  naim::DiskRuntimeState ParseDiskRuntimeStatePayload(const nlohmann::json&) const override {
    return {};
  }

  std::string Trim(const std::string& value) const override {
    return value;
  }

  struct Request {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
  };

  std::string session_token = naim::RandomTokenBase64(32);
  mutable std::vector<Request> requests;
};

void TestTransitionOpensSessionWhenNeeded() {
  const auto keypair = naim::GenerateSigningKeypair();
  FakeHttpHostdBackendSupport support;
  naim::hostd::HttpHostdBackend backend(
      "http://controller",
      keypair.private_key_base64,
      "",
      "",
      "hpc1",
      "/storage",
      support);

  Expect(
      backend.TransitionClaimedHostAssignment(
          42,
          naim::HostAssignmentStatus::Applied,
          "applied"),
      "transition should succeed");

  Expect(support.requests.size() == 3, "transition should open session, heartbeat, and post status");
  Expect(
      support.requests[0].path == "/api/v1/hostd/session/open",
      "transition should open a host session before encrypted status update");
  Expect(
      support.requests[1].path == "/api/v1/hostd/session/heartbeat",
      "transition should heartbeat the new host session");
  Expect(
      support.requests[2].path == "/api/v1/hostd/assignments/42/applied",
      "transition should post the assignment status");
  Expect(
      support.requests[2].headers.count("X-Naim-Host-Session") == 1,
      "assignment transition should include host session header");
}

}  // namespace

int main() {
  try {
    TestTransitionOpensSessionWhenNeeded();
    std::cout << "ok: http-hostd-backend-transition-opens-session\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
