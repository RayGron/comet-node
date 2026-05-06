#include <unistd.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "auth/auth_support_service.h"
#include "interaction/interaction_stream_http_request_preparation_service.h"
#include "naim/state/sqlite_store.h"

namespace {

void Expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string MakeTempDbPath(const std::string& stem) {
  const auto path = std::filesystem::temp_directory_path() /
                    (stem + "-" + std::to_string(::getpid()) + ".db");
  std::filesystem::remove(path);
  return path.string();
}

naim::controller::PlaneInteractionResolution BuildReadyResolution(
    const std::string& plane_name,
    bool protected_plane = false) {
  naim::controller::PlaneInteractionResolution resolution;
  resolution.desired_state.plane_name = plane_name;
  resolution.desired_state.protected_plane = protected_plane;
  resolution.status_payload = {
      {"plane_name", plane_name},
      {"interaction_enabled", true},
      {"ready", true},
      {"reason", "ready"},
      {"served_model_name", "demo-served"},
      {"active_model_id", "demo-model"},
  };
  resolution.target = naim::controller::ControllerEndpointTarget{
      "http://127.0.0.1:8080",
      "127.0.0.1",
      8080,
      "",
  };
  return resolution;
}

void TestPrepareBuildsSetupForReadyUnprotectedPlane() {
  const std::string db_path = MakeTempDbPath("interaction-stream-http-prepare");
  const naim::controller::InteractionStreamRequestResolver resolver(
      [](const std::string&, const std::string& plane_name) {
        return BuildReadyResolution(plane_name);
      },
      [](const naim::controller::PlaneInteractionResolution&,
         naim::controller::InteractionRequestContext*) {
        return std::optional<naim::controller::InteractionValidationError>{};
      });
  const naim::controller::InteractionStreamHttpRequestPreparationService service(
      resolver,
      [](const naim::controller::PlaneInteractionResolution&,
         naim::controller::InteractionRequestContext*) {
        return std::optional<naim::controller::InteractionValidationError>{};
      });
  AuthSupportService auth_support;
  HttpRequest request;
  request.method = "POST";
  request.path = "/api/v1/planes/demo-plane/interaction/chat/completions/stream";
  request.body = R"({"messages":[{"role":"user","content":"hello"}]})";

  const auto result = service.Prepare(db_path, request, "req-1", auth_support);
  Expect(!result.error_response.has_value(),
         "ready unprotected plane should not return an error");
  Expect(result.setup.has_value(), "ready unprotected plane should return setup");
  Expect(result.setup->plane_name == "demo-plane",
         "setup should preserve plane name");
  Expect(result.setup->request_context.request_id == "req-1",
         "setup should preserve request id");
  Expect(!result.setup->request_context.conversation_session_id.empty(),
         "setup should prepare conversation session id");
  Expect(result.setup->resolved_policy.mode == "default-fallback",
         "setup should resolve fallback completion policy");
  std::cout << "ok: interaction-stream-http-prepare-ready-plane" << '\n';

  std::filesystem::remove(db_path);
}

void TestPrepareRejectsUnauthorizedProtectedPlane() {
  const std::string db_path =
      MakeTempDbPath("interaction-stream-http-prepare-protected");
  const naim::controller::InteractionStreamRequestResolver resolver(
      [](const std::string&, const std::string& plane_name) {
        return BuildReadyResolution(plane_name, true);
      },
      [](const naim::controller::PlaneInteractionResolution&,
         naim::controller::InteractionRequestContext*) {
        return std::optional<naim::controller::InteractionValidationError>{};
      });
  const naim::controller::InteractionStreamHttpRequestPreparationService service(
      resolver,
      [](const naim::controller::PlaneInteractionResolution&,
         naim::controller::InteractionRequestContext*) {
        return std::optional<naim::controller::InteractionValidationError>{};
      });
  AuthSupportService auth_support;
  HttpRequest request;
  request.method = "POST";
  request.path = "/api/v1/planes/protected-plane/interaction/chat/completions/stream";
  request.body = R"({"messages":[{"role":"user","content":"hello"}]})";

  const auto result = service.Prepare(db_path, request, "req-2", auth_support);
  Expect(result.error_response.has_value(),
         "protected plane without auth should return an error");
  Expect(result.error_response->status_code == 401,
         "protected plane without auth should return 401");
  const auto payload = nlohmann::json::parse(result.error_response->body);
  Expect(payload.at("error").at("code").get<std::string>() == "unauthorized",
         "protected plane error should preserve unauthorized code");
  std::cout << "ok: interaction-stream-http-prepare-protected-plane" << '\n';

  std::filesystem::remove(db_path);
}

void TestPrepareUsesExternalOwnerForSecuredConnectionSession() {
  const std::string db_path =
      MakeTempDbPath("interaction-stream-http-prepare-secured");
  naim::ControllerStore store(db_path);
  store.Initialize();
  store.UpsertSecuredConnectionUser({
      "scu-test",
      "terminal-a",
      "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAITestOnly test@example",
      "SHA256:test",
      "terminal-a SHA256:test",
      "",
      "2026-05-06 00:00:00",
      "2026-05-06 00:00:00",
      "",
  });
  store.InsertSecuredConnectionSession({
      "secured-token-test",
      "scu-test",
      "protected-plane",
      "2099-01-01 00:00:00",
      "2026-05-06 00:00:00",
      "",
      "",
  });

  const naim::controller::InteractionStreamRequestResolver resolver(
      [](const std::string&, const std::string& plane_name) {
        return BuildReadyResolution(plane_name, true);
      },
      [](const naim::controller::PlaneInteractionResolution&,
         naim::controller::InteractionRequestContext*) {
        return std::optional<naim::controller::InteractionValidationError>{};
      });
  const naim::controller::InteractionStreamHttpRequestPreparationService service(
      resolver,
      [](const naim::controller::PlaneInteractionResolution&,
         naim::controller::InteractionRequestContext*) {
        return std::optional<naim::controller::InteractionValidationError>{};
      });
  AuthSupportService auth_support;
  HttpRequest request;
  request.method = "POST";
  request.path = "/api/v1/planes/protected-plane/interaction/chat/completions/stream";
  request.headers["x-naim-session-token"] = "secured-token-test";
  request.body = R"({"messages":[{"role":"user","content":"hello"}]})";

  const auto result = service.Prepare(db_path, request, "req-3", auth_support);
  Expect(!result.error_response.has_value(),
         "secured connection session should authorize protected plane request");
  Expect(result.setup.has_value(), "secured connection request should return setup");
  Expect(
      result.setup->request_context.owner_kind ==
          "secured_connection:7fff86ef51c3be788e2bafec",
      "secured connection conversation owner should not use users table ids");
  Expect(
      !result.setup->request_context.owner_user_id.has_value(),
      "secured connection conversation owner_user_id should remain null");
  Expect(
      result.setup->request_context.auth_session_kind == "secured_ssh",
      "secured connection auth session kind should be preserved");
  std::cout << "ok: interaction-stream-http-prepare-secured-owner" << '\n';

  std::filesystem::remove(db_path);
}

}  // namespace

int main() {
  try {
    TestPrepareBuildsSetupForReadyUnprotectedPlane();
    TestPrepareRejectsUnauthorizedProtectedPlane();
    TestPrepareUsesExternalOwnerForSecuredConnectionSession();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "interaction_stream_http_request_preparation_service_tests failed: "
              << error.what() << '\n';
    return 1;
  }
}
