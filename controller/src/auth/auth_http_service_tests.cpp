#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "auth/auth_http_service.h"
#include "auth/auth_http_support.h"
#include "auth/auth_support_service.h"
#include "naim/security/crypto_utils.h"
#include "naim/state/desired_state_v2_renderer.h"
#include "naim/state/desired_state_v2_validator.h"
#include "naim/state/sqlite_store.h"

namespace fs = std::filesystem;

namespace {

using nlohmann::json;

void Expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string ReadFile(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    throw std::runtime_error("failed to read file: " + path.string());
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

void WriteFile(const fs::path& path, const std::string& content) {
  std::ofstream output(path, std::ios::binary);
  if (!output.is_open()) {
    throw std::runtime_error("failed to write file: " + path.string());
  }
  output << content;
  output.close();
  if (!output) {
    throw std::runtime_error("failed to write file: " + path.string());
  }
}

void RunShellOrThrow(const std::string& command, const std::string& error) {
  const int rc = std::system(command.c_str());
  if (rc != 0) {
    throw std::runtime_error(error + " (rc=" + std::to_string(rc) + ")");
  }
}

struct TempDir {
  fs::path path;

  ~TempDir() {
    std::error_code error;
    fs::remove_all(path, error);
  }
};

TempDir MakeTempDir(const std::string& prefix) {
  TempDir dir;
  std::string token = naim::RandomTokenBase64(8);
  for (char& ch : token) {
    const unsigned char byte = static_cast<unsigned char>(ch);
    if (!std::isalnum(byte) && ch != '-' && ch != '_') {
      ch = '_';
    }
  }
  dir.path = fs::temp_directory_path() / (prefix + "-" + token);
  fs::create_directories(dir.path);
  return dir;
}

naim::DesiredState BuildProtectedDesiredState(const std::string& plane_name) {
  json value{
      {"version", 2},
      {"plane_name", plane_name},
      {"plane_mode", "llm"},
      {"protected", true},
      {"model",
       {
           {"source", {{"type", "local"}, {"path", "/models/demo"}}},
           {"materialization", {{"mode", "reference"}, {"local_path", "/models/demo"}}},
           {"served_model_name", plane_name + "-model"},
       }},
      {"runtime",
       {{"engine", "llama.cpp"}, {"distributed_backend", "llama_rpc"}, {"workers", 1}}},
      {"infer", {{"replicas", 1}}},
      {"skills",
       {
           {"enabled", true},
           {"factory_skill_ids", json::array()},
       }},
      {"app", {{"enabled", false}}},
  };
  naim::DesiredStateV2Validator::ValidateOrThrow(value);
  return naim::DesiredStateV2Renderer::Render(value);
}

naim::DesiredState BuildSecuredDesiredState(
    const std::string& plane_name,
    const std::string& secured_user_id) {
  json value{
      {"version", 2},
      {"plane_name", plane_name},
      {"plane_mode", "llm"},
      {"model",
       {
           {"source", {{"type", "local"}, {"path", "/models/demo"}}},
           {"materialization", {{"mode", "reference"}, {"local_path", "/models/demo"}}},
           {"served_model_name", plane_name + "-model"},
       }},
      {"features",
       {{"secured_connection",
         {{"enabled", true}, {"user_ids", json::array({secured_user_id})}}}}},
      {"runtime",
       {{"engine", "llama.cpp"}, {"distributed_backend", "llama_rpc"}, {"workers", 1}}},
      {"infer", {{"replicas", 1}}},
      {"app", {{"enabled", false}}},
  };
  naim::DesiredStateV2Validator::ValidateOrThrow(value);
  return naim::DesiredStateV2Renderer::Render(value);
}

std::string ExtractCookieToken(const std::string& set_cookie) {
  const auto prefix = std::string("naim.sid=");
  Expect(set_cookie.rfind(prefix, 0) == 0, "Set-Cookie must start with naim.sid=");
  const auto semicolon = set_cookie.find(';', prefix.size());
  const auto token =
      set_cookie.substr(prefix.size(), semicolon == std::string::npos ? std::string::npos
                                                                      : semicolon - prefix.size());
  Expect(!token.empty(), "cookie token must not be empty");
  return token;
}

void TestSshVerifyCanIssueCookieOnlySession() {
  auto temp = MakeTempDir("naim-auth-http-tests");
  const auto db_path = temp.path / "controller.sqlite";

  naim::ControllerStore store(db_path.string());
  store.Initialize();
  store.ReplaceDesiredState(BuildProtectedDesiredState("maglev"), 1);
  const auto user =
      store.CreateBootstrapAdmin("baal", naim::HashPassword("secret"));

  const auto key_path = temp.path / "id_ed25519";
  const auto message_path = temp.path / "message.txt";
  RunShellOrThrow(
      "ssh-keygen -q -t ed25519 -N '' -f '" + key_path.string() + "' >/dev/null 2>/dev/null",
      "failed to generate ssh test key");
  const auto public_key = ReadFile(key_path.string() + ".pub");

  AuthSupportService auth_support;
  const auto fingerprint =
      auth_support.ComputeSshPublicKeyFingerprint(public_key);
  store.InsertUserSshKey(naim::UserSshKeyRecord{
      0,
      user.id,
      "test-key",
      public_key,
      fingerprint,
      "",
      "",
      "",
  });

  AuthHttpSupport http_support(auth_support);
  AuthHttpService service(std::move(http_support));

  HttpRequest challenge_request;
  challenge_request.method = "POST";
  challenge_request.path = "/api/v1/auth/ssh/challenge";
  challenge_request.body = json{
      {"username", user.username},
      {"plane_name", "maglev"},
      {"fingerprint", fingerprint},
  }.dump();

  const auto challenge_response =
      service.HandleRequest(db_path.string(), challenge_request);
  Expect(challenge_response.has_value(), "ssh challenge route should respond");
  Expect(challenge_response->status_code == 200, "ssh challenge should succeed");
  const auto challenge_payload = json::parse(challenge_response->body);
  const auto challenge_id =
      challenge_payload.at("challenge_id").get<std::string>();
  const auto message = challenge_payload.at("message").get<std::string>();
  WriteFile(message_path, message);
  RunShellOrThrow(
      "ssh-keygen -Y sign -f '" + key_path.string() +
          "' -n naim-plane-auth '" + message_path.string() +
          "' >/dev/null 2>/dev/null",
      "failed to sign ssh challenge");
  const auto signature = ReadFile(message_path.string() + ".sig");
  Expect(
      auth_support.VerifySshDetachedSignature(
          user.username, public_key, message, signature),
      "generated ssh signature should verify");

  HttpRequest verify_request;
  verify_request.method = "POST";
  verify_request.path = "/api/v1/auth/ssh/verify";
  verify_request.headers["host"] = "127.0.0.1:18081";
  verify_request.body = json{
      {"challenge_id", challenge_id},
      {"signature", signature},
      {"issue_cookie", true},
  }.dump();

  const auto verify_response =
      service.HandleRequest(db_path.string(), verify_request);
  Expect(verify_response.has_value(), "ssh verify route should respond");
  Expect(verify_response->status_code == 200, "ssh verify should succeed");
  Expect(
      verify_response->headers.contains("Set-Cookie"),
      "ssh verify should issue session cookie when requested");
  const auto verify_payload = json::parse(verify_response->body);
  Expect(
      verify_payload.value("issued_cookie", false),
      "ssh verify should mark issued_cookie in payload");
  Expect(
      !verify_payload.contains("token"),
      "ssh verify should omit token when issue_cookie=true uses safe default");

  const auto session_token =
      ExtractCookieToken(verify_response->headers.at("Set-Cookie"));
  HttpRequest protected_request;
  protected_request.path = "/api/v1/planes/maglev/skills";
  protected_request.headers["cookie"] = "naim.sid=" + session_token;
  const auto authenticated =
      auth_support.AuthenticateProtectedPlaneRequest(
          store, protected_request, "maglev");
  Expect(
      authenticated.has_value(),
      "protected-plane auth should accept naim.sid issued by ssh verify");

  std::cout << "ok: ssh-verify-cookie-only-session" << '\n';
}

void TestUserStorageAdminApiAndSecuredChallengeGuards() {
  auto temp = MakeTempDir("naim-user-storage-tests");
  const auto db_path = temp.path / "controller.sqlite";

  naim::ControllerStore store(db_path.string());
  store.Initialize();
  const auto admin =
      store.CreateBootstrapAdmin("admin", naim::HashPassword("secret"));

  const auto key_path = temp.path / "secured_ed25519";
  RunShellOrThrow(
      "ssh-keygen -q -t ed25519 -N '' -f '" + key_path.string() + "' >/dev/null 2>/dev/null",
      "failed to generate secured ssh test key");
  const auto public_key = ReadFile(key_path.string() + ".pub");

  AuthSupportService auth_support;
  AuthHttpSupport http_support(auth_support);
  AuthHttpService service(std::move(http_support));
  const std::string admin_token =
      auth_support.CreateControllerSession(store, admin.id, "web");

  HttpRequest create_request;
  create_request.method = "POST";
  create_request.path = "/api/v1/user-storage";
  create_request.headers["x-naim-session-token"] = admin_token;
  create_request.body = json{
      {"name", "terminal-a"},
      {"public_key", public_key},
  }.dump();

  const auto create_response =
      service.HandleRequest(db_path.string(), create_request);
  Expect(create_response.has_value(), "user-storage create route should respond");
  Expect(create_response->status_code == 201, "user-storage create should succeed");
  const auto created_user =
      json::parse(create_response->body).at("item");
  const std::string secured_user_id = created_user.at("id").get<std::string>();
  const std::string fingerprint = created_user.at("fingerprint").get<std::string>();
  Expect(!secured_user_id.empty(), "user-storage create should return id");
  Expect(!fingerprint.empty(), "user-storage create should return fingerprint");

  HttpRequest list_request;
  list_request.method = "GET";
  list_request.path = "/api/v1/user-storage";
  list_request.headers["x-naim-session-token"] = admin_token;
  list_request.query_params["search"] = "terminal-a";
  const auto list_response = service.HandleRequest(db_path.string(), list_request);
  Expect(list_response.has_value(), "user-storage list route should respond");
  Expect(list_response->status_code == 200, "user-storage list should succeed");
  Expect(
      json::parse(list_response->body).at("items").size() == 1,
      "user-storage search should match created user");

  store.ReplaceDesiredState(BuildSecuredDesiredState("secured-plane", secured_user_id), 1);

  HttpRequest insecure_challenge;
  insecure_challenge.method = "POST";
  insecure_challenge.path = "/api/v1/auth/ssh/challenge";
  insecure_challenge.headers["host"] = "public.example";
  insecure_challenge.body = json{
      {"name", "terminal-a"},
      {"plane_name", "secured-plane"},
      {"fingerprint", fingerprint},
  }.dump();
  const auto insecure_response =
      service.HandleRequest(db_path.string(), insecure_challenge);
  Expect(insecure_response.has_value(), "secured challenge route should respond");
  Expect(
      insecure_response->status_code == 403,
      "secured challenge should reject non-local HTTP transport");

  store.ReplaceDesiredState(BuildSecuredDesiredState("secured-plane", "other-user"), 2);
  HttpRequest forbidden_challenge = insecure_challenge;
  forbidden_challenge.headers["x-forwarded-proto"] = "https";
  const auto forbidden_response =
      service.HandleRequest(db_path.string(), forbidden_challenge);
  Expect(forbidden_response.has_value(), "secured allowlist route should respond");
  Expect(
      forbidden_response->status_code == 403,
      "secured challenge should reject users missing from plane allowlist");

  store.ReplaceDesiredState(BuildSecuredDesiredState("secured-plane", secured_user_id), 3);
  const auto accepted_response =
      service.HandleRequest(db_path.string(), forbidden_challenge);
  Expect(accepted_response.has_value(), "secured accepted route should respond");
  Expect(
      accepted_response->status_code == 200,
      "secured challenge should accept HTTPS and selected User Storage user");
  Expect(
      store.LoadSecuredConnectionAuthLog("secured-plane", secured_user_id, 10).size() >= 2,
      "secured challenge attempts should be audit logged");

  std::cout << "ok: user-storage-secured-challenge-guards" << '\n';
}

}  // namespace

int main() {
  try {
    TestSshVerifyCanIssueCookieOnlySession();
    TestUserStorageAdminApiAndSecuredChallengeGuards();
    std::cout << "auth_http_service_tests passed" << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "auth_http_service_tests failed: " << error.what() << '\n';
    return 1;
  }
}
