#include "auth/auth_http_service.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

#include "naim/security/crypto_utils.h"

using nlohmann::json;

namespace {

json ParseJsonBody(const HttpRequest& request) {
  if (request.body.empty()) {
    return json::object();
  }
  return json::parse(request.body);
}

bool ParseOptionalBooleanField(
    const json& body,
    const char* key,
    bool default_value) {
  if (!body.contains(key) || body.at(key).is_null()) {
    return default_value;
  }
  if (!body.at(key).is_boolean()) {
    throw std::invalid_argument(std::string(key) + " must be a boolean");
  }
  return body.at(key).get<bool>();
}

bool StartsWithPathPrefix(const std::string& path, const std::string& prefix) {
  return path.rfind(prefix, 0) == 0;
}

std::optional<std::string> FindHeaderString(
    const HttpRequest& request,
    const std::string& key) {
  std::string lowered;
  lowered.reserve(key.size());
  for (unsigned char ch : key) {
    lowered.push_back(static_cast<char>(std::tolower(ch)));
  }
  const auto it = request.headers.find(lowered);
  if (it == request.headers.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::string LowercaseCopy(const std::string& value) {
  std::string lowered;
  lowered.reserve(value.size());
  for (unsigned char ch : value) {
    lowered.push_back(static_cast<char>(std::tolower(ch)));
  }
  return lowered;
}

std::string TrimCopy(const std::string& value) {
  const auto begin = std::find_if_not(
      value.begin(),
      value.end(),
      [](unsigned char ch) { return std::isspace(ch) != 0; });
  const auto end = std::find_if_not(
                       value.rbegin(),
                       value.rend(),
                       [](unsigned char ch) { return std::isspace(ch) != 0; })
                       .base();
  if (begin >= end) {
    return "";
  }
  return std::string(begin, end);
}

std::string FirstForwardedValue(const std::string& value) {
  const auto comma = value.find(',');
  return comma == std::string::npos ? value : value.substr(0, comma);
}

std::string RequestScheme(const HttpRequest& request) {
  if (const auto forwarded = FindHeaderString(request, "x-forwarded-proto");
      forwarded.has_value()) {
    return LowercaseCopy(TrimCopy(FirstForwardedValue(*forwarded)));
  }
  if (const auto forwarded = FindHeaderString(request, "x-forwarded-ssl");
      forwarded.has_value() && LowercaseCopy(*forwarded) == "on") {
    return "https";
  }
  if (const auto forwarded = FindHeaderString(request, "x-forwarded-port");
      forwarded.has_value() && *forwarded == "443") {
    return "https";
  }
  return "http";
}

std::string HostWithoutPort(const std::string& host) {
  if (host.empty()) {
    return host;
  }
  if (host.front() == '[') {
    const auto close = host.find(']');
    return close == std::string::npos ? host : host.substr(1, close - 1);
  }
  const auto colon = host.find(':');
  return colon == std::string::npos ? host : host.substr(0, colon);
}

bool IsLocalHostName(const std::string& host) {
  const std::string lowered = LowercaseCopy(HostWithoutPort(host));
  return lowered.empty() || lowered == "localhost" || lowered == "127.0.0.1" ||
         lowered == "::1";
}

bool IsSecuredRequestTransportAllowed(const HttpRequest& request) {
  if (RequestScheme(request) == "https") {
    return true;
  }
  auto host = FindHeaderString(request, "x-forwarded-host");
  if (!host.has_value()) {
    host = FindHeaderString(request, "host");
  }
  return IsLocalHostName(host.value_or(""));
}

std::string RequestRemoteAddress(const HttpRequest& request) {
  if (const auto forwarded = FindHeaderString(request, "x-forwarded-for");
      forwarded.has_value()) {
    return FirstForwardedValue(*forwarded);
  }
  if (const auto real_ip = FindHeaderString(request, "x-real-ip"); real_ip.has_value()) {
    return *real_ip;
  }
  return "";
}

std::string BuildSecuredUserSearchText(
    const naim::SecuredConnectionUserRecord& user) {
  return user.id + " " + user.name + " " + user.public_key + " " +
         user.fingerprint + " " + user.last_authorized_at + " " +
         user.created_at + " " + user.updated_at;
}

json BuildSecuredConnectionUserPayload(
    const naim::SecuredConnectionUserRecord& user) {
  return json{
      {"id", user.id},
      {"name", user.name},
      {"public_key", user.public_key},
      {"fingerprint", user.fingerprint},
      {"last_authorized_at",
       user.last_authorized_at.empty() ? json(nullptr) : json(user.last_authorized_at)},
      {"created_at", user.created_at},
      {"updated_at", user.updated_at},
      {"revoked_at", user.revoked_at.empty() ? json(nullptr) : json(user.revoked_at)},
  };
}

json BuildSecuredConnectionAuthLogPayload(
    const naim::SecuredConnectionAuthLogRecord& log) {
  json payload = json::parse(log.payload_json.empty() ? "{}" : log.payload_json, nullptr, false);
  if (payload.is_discarded()) {
    payload = json::object();
  }
  return json{
      {"id", log.id},
      {"plane_name", log.plane_name},
      {"user_id", log.user_id},
      {"user_name", log.user_name},
      {"fingerprint", log.fingerprint},
      {"event_type", log.event_type},
      {"outcome", log.outcome},
      {"remote_addr", log.remote_addr},
      {"message", log.message},
      {"created_at", log.created_at},
      {"payload", std::move(payload)},
  };
}

bool ContainsString(const std::vector<std::string>& items, const std::string& value) {
  return std::find(items.begin(), items.end(), value) != items.end();
}

constexpr int InviteLifetimeSeconds() {
  return 60 * 60;
}

constexpr int SshChallengeLifetimeSeconds() {
  return 5 * 60;
}

constexpr int SshSessionLifetimeSeconds() {
  return 60 * 60;
}

}  // namespace

AuthHttpService::AuthHttpService(AuthHttpSupport support) : support_(std::move(support)) {}

std::optional<HttpResponse> AuthHttpService::HandleRequest(
    const std::string& db_path,
    const HttpRequest& request) const {
  if (request.path == "/api/v1/user-storage") {
    return HandleUserStorage(db_path, request);
  }
  if (request.path == "/api/v1/user-storage/auth-log") {
    return HandleUserStorageAuthLog(db_path, request);
  }
  if (StartsWithPathPrefix(request.path, "/api/v1/user-storage/")) {
    return HandleUserStorageItem(
        db_path,
        request,
        request.path.substr(std::string("/api/v1/user-storage/").size()));
  }
  if (!StartsWithPathPrefix(request.path, "/api/v1/auth/")) {
    return std::nullopt;
  }
  if (request.path == "/api/v1/auth/state") {
    return HandleState(db_path, request);
  }
  if (request.path == "/api/v1/auth/me") {
    return HandleMe(db_path, request);
  }
  if (request.path == "/api/v1/auth/logout") {
    return HandleLogout(request);
  }
  if (request.path == "/api/v1/auth/bootstrap/begin") {
    return HandleBootstrapBegin(db_path, request);
  }
  if (request.path == "/api/v1/auth/bootstrap/finish") {
    return HandleBootstrapFinish(db_path, request);
  }
  if (request.path == "/api/v1/auth/login/begin") {
    return HandleLoginBegin(db_path, request);
  }
  if (request.path == "/api/v1/auth/login/finish") {
    return HandleLoginFinish(db_path, request);
  }
  if (StartsWithPathPrefix(request.path, "/api/v1/auth/invite/")) {
    return HandleInviteLookup(db_path, request);
  }
  if (request.path == "/api/v1/auth/register/begin") {
    return HandleRegisterBegin(db_path, request);
  }
  if (request.path == "/api/v1/auth/register/finish") {
    return HandleRegisterFinish(db_path, request);
  }
  if (request.path == "/api/v1/auth/invites") {
    return HandleInvites(db_path, request);
  }
  if (StartsWithPathPrefix(request.path, "/api/v1/auth/invites/")) {
    return HandleInviteDelete(db_path, request);
  }
  if (request.path == "/api/v1/auth/ssh-keys") {
    return HandleSshKeys(db_path, request);
  }
  if (StartsWithPathPrefix(request.path, "/api/v1/auth/ssh-keys/")) {
    return HandleSshKeyDelete(db_path, request);
  }
  if (request.path == "/api/v1/auth/ssh/challenge") {
    return HandleSshChallenge(db_path, request);
  }
  if (request.path == "/api/v1/auth/ssh/verify") {
    return HandleSshVerify(db_path, request);
  }
  return support_.build_json_response(404, json{{"status", "not_found"}}, {});
}

HttpResponse AuthHttpService::HandleState(
    const std::string& db_path,
    const HttpRequest& request) const {
  if (request.method != "GET") {
    return support_.build_json_response(405, json{{"status", "method_not_allowed"}}, {});
  }
  try {
    naim::ControllerStore store(db_path);
    store.Initialize();
    const auto session =
        support_.authenticate_controller_user_session(store, request);
    return support_.build_json_response(
        200,
        json{
            {"service", "naim-controller"},
            {"setup_required", store.LoadUserCount() == 0},
            {"authenticated", session.has_value()},
            {"user",
             session.has_value() ? support_.build_user_payload(session->first)
                                 : json(nullptr)},
            {"rp_id", support_.resolve_webauthn_rp_id(request)},
            {"origin", support_.resolve_webauthn_origin(request)},
        },
        {});
  } catch (const std::exception& error) {
    return support_.build_json_response(
        500, json{{"status", "internal_error"}, {"message", error.what()}}, {});
  }
}

HttpResponse AuthHttpService::HandleMe(
    const std::string& db_path,
    const HttpRequest& request) const {
  if (request.method != "GET") {
    return support_.build_json_response(405, json{{"status", "method_not_allowed"}}, {});
  }
  try {
    naim::ControllerStore store(db_path);
    store.Initialize();
    const auto session =
        support_.authenticate_controller_user_session(store, request);
    if (!session.has_value()) {
      return support_.build_json_response(
          401,
          json{{"status", "unauthorized"},
               {"message", "authentication required"}},
          {{"Set-Cookie", support_.clear_session_cookie_header(request)}});
    }
    return support_.build_json_response(
        200, json{{"user", support_.build_user_payload(session->first)}}, {});
  } catch (const std::exception& error) {
    return support_.build_json_response(
        500, json{{"status", "internal_error"}, {"message", error.what()}}, {});
  }
}

HttpResponse AuthHttpService::HandleLogout(const HttpRequest& request) const {
  if (request.method != "POST") {
    return support_.build_json_response(405, json{{"status", "method_not_allowed"}}, {});
  }
  return support_.build_json_response(
      200,
      json{{"status", "logged_out"}},
      {{"Set-Cookie", support_.clear_session_cookie_header(request)}});
}

HttpResponse AuthHttpService::HandleBootstrapBegin(
    const std::string& db_path,
    const HttpRequest& request) const {
  if (request.method != "POST") {
    return support_.build_json_response(405, json{{"status", "method_not_allowed"}}, {});
  }
  try {
    const json body = ParseJsonBody(request);
    const std::string username = support_.trim(body.value("username", std::string{}));
    const std::string password = body.value("password", std::string{});
    if (username.empty() || password.empty()) {
      return support_.build_json_response(
          400,
          json{{"status", "bad_request"},
               {"message", "username and password are required"}},
          {});
    }
    naim::ControllerStore store(db_path);
    store.Initialize();
    if (store.LoadUserCount() != 0) {
      return support_.build_json_response(
          409,
          json{{"status", "conflict"},
               {"message", "bootstrap is only available before the first user is created"}},
          {});
    }
    const std::string flow_id = naim::RandomTokenBase64(24);
    const std::string challenge = naim::RandomTokenBase64(32);
    const PendingWebAuthnFlow flow{
        flow_id,
        "bootstrap",
        username,
        naim::HashPassword(password),
        "",
        0,
        challenge,
        support_.resolve_webauthn_rp_id(request),
        support_.resolve_webauthn_origin(request),
        support_.sql_timestamp_after_seconds(5 * 60),
    };
    const json options = support_.run_webauthn_helper(
        "generate-registration-options",
        json{{"rpName", support_.resolve_webauthn_rp_name()},
             {"rpID", flow.rp_id},
             {"userName", username},
             {"challenge", challenge}});
    support_.save_pending_webauthn_flow(flow);
    return support_.build_json_response(
        200,
        json{{"flow_id", flow_id},
             {"expires_at", flow.expires_at},
             {"options", options}},
        {});
  } catch (const std::exception& error) {
    return support_.build_json_response(
        500,
        json{{"status", "internal_error"},
             {"message", error.what()},
             {"path", request.path}},
        {});
  }
}

HttpResponse AuthHttpService::HandleBootstrapFinish(
    const std::string& db_path,
    const HttpRequest& request) const {
  if (request.method != "POST") {
    return support_.build_json_response(405, json{{"status", "method_not_allowed"}}, {});
  }
  try {
    const json body = ParseJsonBody(request);
    const std::string flow_id = body.value("flow_id", std::string{});
    if (flow_id.empty() || !body.contains("response")) {
      return support_.build_json_response(
          400,
          json{{"status", "bad_request"},
               {"message", "flow_id and response are required"}},
          {});
    }
    const auto flow = support_.load_pending_webauthn_flow(flow_id);
    if (!flow.has_value() || flow->flow_kind != "bootstrap" ||
        flow->expires_at < support_.utc_now_sql_timestamp()) {
      return support_.build_json_response(
          410,
          json{{"status", "expired"},
               {"message", "bootstrap flow is missing or expired"}},
          {});
    }
    const json verification = support_.run_webauthn_helper(
        "verify-registration",
        json{{"response", body.at("response")},
             {"expectedChallenge", flow->challenge},
             {"expectedOrigin", flow->origin},
             {"expectedRPID", flow->rp_id}});
    if (!verification.value("verified", false)) {
      return support_.build_json_response(
          403,
          json{{"status", "forbidden"},
               {"message", "WebAuthn registration verification failed"}},
          {});
    }
    naim::ControllerStore store(db_path);
    store.Initialize();
    const auto user =
        store.CreateBootstrapAdmin(flow->username, flow->password_hash);
    const json credential = verification.at("registrationInfo");
    store.InsertWebAuthnCredential(naim::WebAuthnCredentialRecord{
        0,
        user.id,
        credential.value("credentialID", std::string{}),
        credential.value("credentialPublicKey", std::string{}),
        static_cast<std::uint32_t>(credential.value("counter", 0)),
        json(credential.value("transports", json::array())).dump(),
        "",
        "",
        "",
    });
    const std::string session_token =
        support_.create_controller_session(store, user.id, "web", "");
    support_.erase_pending_webauthn_flow(flow_id);
    return support_.build_json_response(
        200,
        json{{"user", support_.build_user_payload(user)}},
        {{"Set-Cookie", support_.session_cookie_header(session_token, request)}});
  } catch (const std::exception& error) {
    return support_.build_json_response(
        500,
        json{{"status", "internal_error"},
             {"message", error.what()},
             {"path", request.path}},
        {});
  }
}

HttpResponse AuthHttpService::HandleLoginBegin(
    const std::string& db_path,
    const HttpRequest& request) const {
  if (request.method != "POST") {
    return support_.build_json_response(405, json{{"status", "method_not_allowed"}}, {});
  }
  try {
    const json body = ParseJsonBody(request);
    const std::string username = support_.trim(body.value("username", std::string{}));
    if (username.empty()) {
      return support_.build_json_response(
          400,
          json{{"status", "bad_request"}, {"message", "username is required"}},
          {});
    }
    naim::ControllerStore store(db_path);
    store.Initialize();
    const auto user = store.LoadUserByUsername(username);
    if (!user.has_value()) {
      return support_.build_json_response(
          404, json{{"status", "not_found"}, {"message", "user not found"}}, {});
    }
    const auto credentials = store.LoadWebAuthnCredentialsForUser(user->id);
    if (credentials.empty()) {
      return support_.build_json_response(
          409,
          json{{"status", "conflict"},
               {"message", "user has no registered WebAuthn credentials"}},
          {});
    }
    json allow_credentials = json::array();
    for (const auto& credential : credentials) {
      json transports = json::array();
      try {
        transports = credential.transports_json.empty()
                         ? json::array()
                         : json::parse(credential.transports_json);
      } catch (...) {
        transports = json::array();
      }
      allow_credentials.push_back(
          json{{"id", credential.credential_id}, {"transports", transports}});
    }
    const std::string flow_id = naim::RandomTokenBase64(24);
    const std::string challenge = naim::RandomTokenBase64(32);
    const PendingWebAuthnFlow flow{
        flow_id,
        "login",
        username,
        "",
        "",
        user->id,
        challenge,
        support_.resolve_webauthn_rp_id(request),
        support_.resolve_webauthn_origin(request),
        support_.sql_timestamp_after_seconds(5 * 60),
    };
    const json options = support_.run_webauthn_helper(
        "generate-authentication-options",
        json{{"rpID", flow.rp_id},
             {"challenge", challenge},
             {"allowCredentials", allow_credentials}});
    support_.save_pending_webauthn_flow(flow);
    return support_.build_json_response(
        200,
        json{{"flow_id", flow_id},
             {"expires_at", flow.expires_at},
             {"options", options}},
        {});
  } catch (const std::exception& error) {
    return support_.build_json_response(
        500, json{{"status", "internal_error"}, {"message", error.what()}}, {});
  }
}

HttpResponse AuthHttpService::HandleLoginFinish(
    const std::string& db_path,
    const HttpRequest& request) const {
  if (request.method != "POST") {
    return support_.build_json_response(405, json{{"status", "method_not_allowed"}}, {});
  }
  try {
    const json body = ParseJsonBody(request);
    const std::string flow_id = body.value("flow_id", std::string{});
    if (flow_id.empty() || !body.contains("response")) {
      return support_.build_json_response(
          400,
          json{{"status", "bad_request"},
               {"message", "flow_id and response are required"}},
          {});
    }
    const auto flow = support_.load_pending_webauthn_flow(flow_id);
    if (!flow.has_value() || flow->flow_kind != "login" ||
        flow->expires_at < support_.utc_now_sql_timestamp()) {
      return support_.build_json_response(
          410,
          json{{"status", "expired"},
               {"message", "login flow is missing or expired"}},
          {});
    }
    naim::ControllerStore store(db_path);
    store.Initialize();
    const std::string credential_id =
        body.at("response").value("id", std::string{});
    const auto credential = store.LoadWebAuthnCredentialById(credential_id);
    if (!credential.has_value() || credential->user_id != flow->user_id) {
      return support_.build_json_response(
          403,
          json{{"status", "forbidden"},
               {"message", "credential is not registered for this user"}},
          {});
    }
    const json verification = support_.run_webauthn_helper(
        "verify-authentication",
        json{
            {"response", body.at("response")},
            {"expectedChallenge", flow->challenge},
            {"expectedOrigin", flow->origin},
            {"expectedRPID", flow->rp_id},
            {"credential",
             {
                 {"id", credential->credential_id},
                 {"publicKey", credential->public_key},
                 {"counter", credential->counter},
                 {"transports",
                  credential->transports_json.empty()
                      ? json::array()
                      : json::parse(credential->transports_json)},
             }},
        });
    if (!verification.value("verified", false)) {
      return support_.build_json_response(
          403,
          json{{"status", "forbidden"},
               {"message", "WebAuthn authentication verification failed"}},
          {});
    }
    const json auth_info = verification.at("authenticationInfo");
    store.UpdateWebAuthnCredentialCounter(
        credential->credential_id,
        static_cast<std::uint32_t>(
            auth_info.value("newCounter", credential->counter)),
        support_.utc_now_sql_timestamp());
    const auto user = store.LoadUserById(flow->user_id);
    if (!user.has_value()) {
      return support_.build_json_response(
          404, json{{"status", "not_found"}, {"message", "user not found"}}, {});
    }
    const std::string session_token =
        support_.create_controller_session(store, user->id, "web", "");
    support_.erase_pending_webauthn_flow(flow_id);
    return support_.build_json_response(
        200,
        json{{"user", support_.build_user_payload(*user)}},
        {{"Set-Cookie", support_.session_cookie_header(session_token, request)}});
  } catch (const std::exception& error) {
    return support_.build_json_response(
        500,
        json{{"status", "internal_error"},
             {"message", error.what()},
             {"path", request.path}},
        {});
  }
}

HttpResponse AuthHttpService::HandleInviteLookup(
    const std::string& db_path,
    const HttpRequest& request) const {
  if (request.method != "GET") {
    return support_.build_json_response(405, json{{"status", "method_not_allowed"}}, {});
  }
  try {
    const std::string token =
        request.path.substr(std::string("/api/v1/auth/invite/").size());
    naim::ControllerStore store(db_path);
    store.Initialize();
    const auto invite = store.LoadRegistrationInviteByToken(token);
    const bool valid = invite.has_value() && invite->revoked_at.empty() &&
                       invite->used_at.empty() &&
                       invite->expires_at >= support_.utc_now_sql_timestamp();
    return support_.build_json_response(
        valid ? 200 : 404,
        json{{"valid", valid},
             {"invite",
              valid ? support_.build_invite_payload(*invite) : json(nullptr)}},
        {});
  } catch (const std::exception& error) {
    return support_.build_json_response(
        500, json{{"status", "internal_error"}, {"message", error.what()}}, {});
  }
}

HttpResponse AuthHttpService::HandleRegisterBegin(
    const std::string& db_path,
    const HttpRequest& request) const {
  if (request.method != "POST") {
    return support_.build_json_response(405, json{{"status", "method_not_allowed"}}, {});
  }
  try {
    const json body = ParseJsonBody(request);
    const std::string invite_token = body.value("invite_token", std::string{});
    const std::string username = support_.trim(body.value("username", std::string{}));
    const std::string password = body.value("password", std::string{});
    if (invite_token.empty() || username.empty() || password.empty()) {
      return support_.build_json_response(
          400,
          json{{"status", "bad_request"},
               {"message",
                "invite_token, username, and password are required"}},
          {});
    }
    naim::ControllerStore store(db_path);
    store.Initialize();
    const auto invite = store.LoadRegistrationInviteByToken(invite_token);
    if (!invite.has_value() || !invite->revoked_at.empty() ||
        !invite->used_at.empty() ||
        invite->expires_at < support_.utc_now_sql_timestamp()) {
      return support_.build_json_response(
          404,
          json{{"status", "not_found"},
               {"message",
                "invite is missing, expired, revoked, or already used"}},
          {});
    }
    if (store.LoadUserByUsername(username).has_value()) {
      return support_.build_json_response(
          409,
          json{{"status", "conflict"},
               {"message", "username is already taken"}},
          {});
    }
    const std::string flow_id = naim::RandomTokenBase64(24);
    const std::string challenge = naim::RandomTokenBase64(32);
    const PendingWebAuthnFlow flow{
        flow_id,
        "register",
        username,
        naim::HashPassword(password),
        invite_token,
        0,
        challenge,
        support_.resolve_webauthn_rp_id(request),
        support_.resolve_webauthn_origin(request),
        support_.sql_timestamp_after_seconds(5 * 60),
    };
    const json options = support_.run_webauthn_helper(
        "generate-registration-options",
        json{{"rpName", support_.resolve_webauthn_rp_name()},
             {"rpID", flow.rp_id},
             {"userName", username},
             {"challenge", challenge}});
    support_.save_pending_webauthn_flow(flow);
    return support_.build_json_response(
        200,
        json{{"flow_id", flow_id},
             {"expires_at", flow.expires_at},
             {"options", options}},
        {});
  } catch (const std::exception& error) {
    return support_.build_json_response(
        500, json{{"status", "internal_error"}, {"message", error.what()}}, {});
  }
}

HttpResponse AuthHttpService::HandleRegisterFinish(
    const std::string& db_path,
    const HttpRequest& request) const {
  if (request.method != "POST") {
    return support_.build_json_response(405, json{{"status", "method_not_allowed"}}, {});
  }
  try {
    const json body = ParseJsonBody(request);
    const std::string flow_id = body.value("flow_id", std::string{});
    if (flow_id.empty() || !body.contains("response")) {
      return support_.build_json_response(
          400,
          json{{"status", "bad_request"},
               {"message", "flow_id and response are required"}},
          {});
    }
    const auto flow = support_.load_pending_webauthn_flow(flow_id);
    if (!flow.has_value() || flow->flow_kind != "register" ||
        flow->expires_at < support_.utc_now_sql_timestamp()) {
      return support_.build_json_response(
          410,
          json{{"status", "expired"},
               {"message", "registration flow is missing or expired"}},
          {});
    }
    const json verification = support_.run_webauthn_helper(
        "verify-registration",
        json{{"response", body.at("response")},
             {"expectedChallenge", flow->challenge},
             {"expectedOrigin", flow->origin},
             {"expectedRPID", flow->rp_id}});
    if (!verification.value("verified", false)) {
      return support_.build_json_response(
          403,
          json{{"status", "forbidden"},
               {"message", "WebAuthn registration verification failed"}},
          {});
    }
    naim::ControllerStore store(db_path);
    store.Initialize();
    const auto user = store.CreateInvitedUser(
        flow->invite_token, flow->username, flow->password_hash);
    const json credential = verification.at("registrationInfo");
    store.InsertWebAuthnCredential(naim::WebAuthnCredentialRecord{
        0,
        user.id,
        credential.value("credentialID", std::string{}),
        credential.value("credentialPublicKey", std::string{}),
        static_cast<std::uint32_t>(credential.value("counter", 0)),
        json(credential.value("transports", json::array())).dump(),
        "",
        "",
        "",
    });
    const std::string session_token =
        support_.create_controller_session(store, user.id, "web", "");
    support_.erase_pending_webauthn_flow(flow_id);
    return support_.build_json_response(
        200,
        json{{"user", support_.build_user_payload(user)}},
        {{"Set-Cookie", support_.session_cookie_header(session_token, request)}});
  } catch (const std::exception& error) {
    return support_.build_json_response(
        500,
        json{{"status", "internal_error"},
             {"message", error.what()},
             {"path", request.path}},
        {});
  }
}

HttpResponse AuthHttpService::HandleInvites(
    const std::string& db_path,
    const HttpRequest& request) const {
  try {
    naim::ControllerStore store(db_path);
    store.Initialize();
    const auto admin = support_.require_controller_admin_user(store, request);
    if (!admin.has_value()) {
      return support_.build_json_response(
          401,
          json{{"status", "unauthorized"},
               {"message", "admin session is required"}},
          {{"Set-Cookie", support_.clear_session_cookie_header(request)}});
    }
    if (request.method == "GET") {
      json items = json::array();
      for (const auto& invite : store.LoadActiveRegistrationInvites()) {
        json item = support_.build_invite_payload(invite);
        item["registration_url"] =
            support_.resolve_webauthn_origin(request) + "/register/" + invite.token;
        items.push_back(std::move(item));
      }
      return support_.build_json_response(200, json{{"items", items}}, {});
    }
    if (request.method == "POST") {
      const std::string token =
          support_.sanitize_token_for_path(naim::RandomTokenBase64(18));
      const auto invite = store.CreateRegistrationInvite(
          admin->id,
          token,
          support_.sql_timestamp_after_seconds(InviteLifetimeSeconds()));
      json item = support_.build_invite_payload(invite);
      item["registration_url"] =
          support_.resolve_webauthn_origin(request) + "/register/" + invite.token;
      return support_.build_json_response(200, json{{"invite", item}}, {});
    }
    return support_.build_json_response(405, json{{"status", "method_not_allowed"}}, {});
  } catch (const std::exception& error) {
    return support_.build_json_response(
        500,
        json{{"status", "internal_error"},
             {"message", error.what()},
             {"path", request.path}},
        {});
  }
}

HttpResponse AuthHttpService::HandleInviteDelete(
    const std::string& db_path,
    const HttpRequest& request) const {
  if (request.method != "DELETE") {
    return support_.build_json_response(405, json{{"status", "method_not_allowed"}}, {});
  }
  try {
    naim::ControllerStore store(db_path);
    store.Initialize();
    const auto admin = support_.require_controller_admin_user(store, request);
    if (!admin.has_value()) {
      return support_.build_json_response(
          401,
          json{{"status", "unauthorized"},
               {"message", "admin session is required"}},
          {{"Set-Cookie", support_.clear_session_cookie_header(request)}});
    }
    const int invite_id = std::stoi(
        request.path.substr(std::string("/api/v1/auth/invites/").size()));
    const bool revoked = store.RevokeRegistrationInvite(
        invite_id, support_.utc_now_sql_timestamp());
    return support_.build_json_response(
        revoked ? 200 : 404, json{{"revoked", revoked}}, {});
  } catch (const std::exception& error) {
    return support_.build_json_response(
        500,
        json{{"status", "internal_error"},
             {"message", error.what()},
             {"path", request.path}},
        {});
  }
}

HttpResponse AuthHttpService::HandleSshKeys(
    const std::string& db_path,
    const HttpRequest& request) const {
  try {
    naim::ControllerStore store(db_path);
    store.Initialize();
    const auto session =
        support_.authenticate_controller_user_session(store, request);
    if (!session.has_value()) {
      return support_.build_json_response(
          401,
          json{{"status", "unauthorized"},
               {"message", "authentication required"}},
          {{"Set-Cookie", support_.clear_session_cookie_header(request)}});
    }
    if (request.method == "GET") {
      json items = json::array();
      for (const auto& key : store.LoadActiveUserSshKeys(session->first.id)) {
        items.push_back(support_.build_ssh_key_payload(key));
      }
      return support_.build_json_response(200, json{{"items", items}}, {});
    }
    if (request.method == "POST") {
      const json body = ParseJsonBody(request);
      const std::string public_key =
          support_.trim(body.value("public_key", std::string{}));
      if (public_key.empty()) {
        return support_.build_json_response(
            400,
            json{{"status", "bad_request"},
                 {"message", "public_key is required"}},
            {});
      }
      const naim::UserSshKeyRecord ssh_key{
          0,
          session->first.id,
          support_.trim(body.value("label", std::string{})),
          public_key,
          support_.compute_ssh_public_key_fingerprint(public_key),
          "",
          "",
          "",
      };
      store.InsertUserSshKey(ssh_key);
      const auto created = store.LoadActiveUserSshKeyByFingerprint(
          session->first.id, ssh_key.fingerprint);
      return support_.build_json_response(
          200,
          json{{"ssh_key",
                created.has_value()
                    ? support_.build_ssh_key_payload(*created)
                    : support_.build_ssh_key_payload(ssh_key)}},
          {});
    }
    return support_.build_json_response(405, json{{"status", "method_not_allowed"}}, {});
  } catch (const std::exception& error) {
    return support_.build_json_response(
        500,
        json{{"status", "internal_error"},
             {"message", error.what()},
             {"path", request.path}},
        {});
  }
}

HttpResponse AuthHttpService::HandleSshKeyDelete(
    const std::string& db_path,
    const HttpRequest& request) const {
  if (request.method != "DELETE") {
    return support_.build_json_response(405, json{{"status", "method_not_allowed"}}, {});
  }
  try {
    naim::ControllerStore store(db_path);
    store.Initialize();
    const auto session =
        support_.authenticate_controller_user_session(store, request);
    if (!session.has_value()) {
      return support_.build_json_response(
          401,
          json{{"status", "unauthorized"},
               {"message", "authentication required"}},
          {{"Set-Cookie", support_.clear_session_cookie_header(request)}});
    }
    const int ssh_key_id = std::stoi(
        request.path.substr(std::string("/api/v1/auth/ssh-keys/").size()));
    const auto ssh_key = store.LoadActiveUserSshKeyById(ssh_key_id);
    if (!ssh_key.has_value() || ssh_key->user_id != session->first.id) {
      return support_.build_json_response(
          404,
          json{{"status", "not_found"}, {"message", "SSH key not found"}},
          {});
    }
    const bool revoked =
        store.RevokeUserSshKey(ssh_key_id, support_.utc_now_sql_timestamp());
    return support_.build_json_response(
        revoked ? 200 : 404, json{{"revoked", revoked}}, {});
  } catch (const std::exception& error) {
    return support_.build_json_response(
        500,
        json{{"status", "internal_error"},
             {"message", error.what()},
             {"path", request.path}},
        {});
  }
}

HttpResponse AuthHttpService::HandleUserStorage(
    const std::string& db_path,
    const HttpRequest& request) const {
  try {
    naim::ControllerStore store(db_path);
    store.Initialize();
    if (!support_.require_controller_admin_user(store, request).has_value()) {
      return support_.build_json_response(
          401,
          json{{"status", "unauthorized"}, {"message", "admin authentication required"}},
          {});
    }
    if (request.method == "GET") {
      std::optional<std::string> search;
      if (const auto it = request.query_params.find("search");
          it != request.query_params.end() && !it->second.empty()) {
        search = it->second;
      }
      const bool include_revoked =
          request.query_params.find("include_revoked") != request.query_params.end() &&
          request.query_params.at("include_revoked") == "true";
      json items = json::array();
      for (const auto& user : store.LoadSecuredConnectionUsers(search, include_revoked)) {
        items.push_back(BuildSecuredConnectionUserPayload(user));
      }
      return support_.build_json_response(200, json{{"items", std::move(items)}}, {});
    }
    if (request.method != "POST") {
      return support_.build_json_response(405, json{{"status", "method_not_allowed"}}, {});
    }
    const json body = ParseJsonBody(request);
    naim::SecuredConnectionUserRecord user;
    user.id = "scu_" + support_.sanitize_token_for_path(naim::RandomTokenBase64(18));
    user.name = support_.trim(body.value("name", std::string{}));
    user.public_key = support_.trim(body.value("public_key", std::string{}));
    if (user.name.empty() || user.public_key.empty()) {
      return support_.build_json_response(
          400,
          json{{"status", "bad_request"},
               {"message", "name and public_key are required"}},
          {});
    }
    user.fingerprint = support_.compute_ssh_public_key_fingerprint(user.public_key);
    user.search_text = BuildSecuredUserSearchText(user);
    store.UpsertSecuredConnectionUser(user);
    const auto saved = store.LoadActiveSecuredConnectionUser(user.id);
    return support_.build_json_response(
        201,
        json{{"item", BuildSecuredConnectionUserPayload(saved.value_or(user))}},
        {});
  } catch (const std::invalid_argument& error) {
    return support_.build_json_response(
        400,
        json{{"status", "bad_request"}, {"message", error.what()}, {"path", request.path}},
        {});
  } catch (const std::exception& error) {
    return support_.build_json_response(
        500,
        json{{"status", "internal_error"}, {"message", error.what()}, {"path", request.path}},
        {});
  }
}

HttpResponse AuthHttpService::HandleUserStorageItem(
    const std::string& db_path,
    const HttpRequest& request,
    const std::string& user_id) const {
  try {
    naim::ControllerStore store(db_path);
    store.Initialize();
    if (!support_.require_controller_admin_user(store, request).has_value()) {
      return support_.build_json_response(
          401,
          json{{"status", "unauthorized"}, {"message", "admin authentication required"}},
          {});
    }
    const auto current = store.LoadActiveSecuredConnectionUser(user_id);
    if (!current.has_value()) {
      return support_.build_json_response(
          404, json{{"status", "not_found"}, {"message", "user not found"}}, {});
    }
    if (request.method == "DELETE") {
      store.RevokeSecuredConnectionUser(user_id, support_.utc_now_sql_timestamp());
      return support_.build_json_response(200, json{{"status", "revoked"}}, {});
    }
    if (request.method != "PATCH") {
      return support_.build_json_response(405, json{{"status", "method_not_allowed"}}, {});
    }
    const json body = ParseJsonBody(request);
    naim::SecuredConnectionUserRecord next = *current;
    if (body.contains("name")) {
      next.name = support_.trim(body.value("name", std::string{}));
    }
    if (body.contains("public_key")) {
      next.public_key = support_.trim(body.value("public_key", std::string{}));
      next.fingerprint = support_.compute_ssh_public_key_fingerprint(next.public_key);
    }
    if (next.name.empty() || next.public_key.empty()) {
      return support_.build_json_response(
          400,
          json{{"status", "bad_request"},
               {"message", "name and public_key are required"}},
          {});
    }
    next.search_text = BuildSecuredUserSearchText(next);
    store.UpsertSecuredConnectionUser(next);
    const auto saved = store.LoadActiveSecuredConnectionUser(user_id);
    return support_.build_json_response(
        200,
        json{{"item", BuildSecuredConnectionUserPayload(saved.value_or(next))}},
        {});
  } catch (const std::invalid_argument& error) {
    return support_.build_json_response(
        400,
        json{{"status", "bad_request"}, {"message", error.what()}, {"path", request.path}},
        {});
  } catch (const std::exception& error) {
    return support_.build_json_response(
        500,
        json{{"status", "internal_error"}, {"message", error.what()}, {"path", request.path}},
        {});
  }
}

HttpResponse AuthHttpService::HandleUserStorageAuthLog(
    const std::string& db_path,
    const HttpRequest& request) const {
  if (request.method != "GET") {
    return support_.build_json_response(405, json{{"status", "method_not_allowed"}}, {});
  }
  try {
    naim::ControllerStore store(db_path);
    store.Initialize();
    if (!support_.require_controller_admin_user(store, request).has_value()) {
      return support_.build_json_response(
          401,
          json{{"status", "unauthorized"}, {"message", "admin authentication required"}},
          {});
    }
    std::optional<std::string> plane_name;
    if (const auto it = request.query_params.find("plane_name");
        it != request.query_params.end() && !it->second.empty()) {
      plane_name = it->second;
    }
    std::optional<std::string> user_id;
    if (const auto it = request.query_params.find("user_id");
        it != request.query_params.end() && !it->second.empty()) {
      user_id = it->second;
    }
    int limit = 100;
    if (const auto it = request.query_params.find("limit");
        it != request.query_params.end() && !it->second.empty()) {
      limit = std::stoi(it->second);
    }
    json items = json::array();
    for (const auto& log : store.LoadSecuredConnectionAuthLog(plane_name, user_id, limit)) {
      items.push_back(BuildSecuredConnectionAuthLogPayload(log));
    }
    return support_.build_json_response(200, json{{"items", std::move(items)}}, {});
  } catch (const std::exception& error) {
    return support_.build_json_response(
        500,
        json{{"status", "internal_error"}, {"message", error.what()}, {"path", request.path}},
        {});
  }
}

HttpResponse AuthHttpService::HandleSshChallenge(
    const std::string& db_path,
    const HttpRequest& request) const {
  if (request.method != "POST") {
    return support_.build_json_response(405, json{{"status", "method_not_allowed"}}, {});
  }
  try {
    const json body = ParseJsonBody(request);
    const std::string username = support_.trim(
        body.value("username", body.value("name", std::string{})));
    const std::string plane_name = support_.trim(body.value("plane_name", std::string{}));
    const std::string fingerprint = support_.trim(body.value("fingerprint", std::string{}));
    if (plane_name.empty() || fingerprint.empty()) {
      return support_.build_json_response(
          400,
          json{{"status", "bad_request"},
               {"message",
                "plane_name and fingerprint are required"}},
          {});
    }
    naim::ControllerStore store(db_path);
    store.Initialize();
    const auto desired_state = store.LoadDesiredState(plane_name);
    if (!desired_state.has_value() || !desired_state->protected_plane) {
      return support_.build_json_response(
          404,
          json{{"status", "not_found"},
               {"message", "protected plane not found"}},
          {});
    }
    const bool secured_enabled =
        desired_state->secured_connection.has_value() &&
        desired_state->secured_connection->enabled;
    if (secured_enabled) {
      if (!IsSecuredRequestTransportAllowed(request)) {
        store.InsertSecuredConnectionAuthLog({
            0,
            plane_name,
            "",
            username,
            fingerprint,
            "challenge",
            "rejected",
            RequestRemoteAddress(request),
            "HTTPS/TLS is required for secured connection authentication",
            "",
            "{}",
        });
        return support_.build_json_response(
            403,
            json{{"status", "forbidden"},
                 {"message", "secured connection authentication requires HTTPS/TLS"}},
            {});
      }
      const auto secured_user =
          store.LoadActiveSecuredConnectionUserByFingerprint(fingerprint);
      if (!secured_user.has_value()) {
        store.InsertSecuredConnectionAuthLog({
            0,
            plane_name,
            "",
            username,
            fingerprint,
            "challenge",
            "rejected",
            RequestRemoteAddress(request),
            "secured connection user or fingerprint not found",
            "",
            "{}",
        });
        return support_.build_json_response(
            404,
            json{{"status", "not_found"},
                 {"message", "secured connection user or fingerprint not found"}},
            {});
      }
      if (!ContainsString(desired_state->secured_connection->user_ids, secured_user->id)) {
        store.InsertSecuredConnectionAuthLog({
            0,
            plane_name,
            secured_user->id,
            secured_user->name,
            fingerprint,
            "challenge",
            "rejected",
            RequestRemoteAddress(request),
            "user is not allowed for secured plane",
            "",
            "{}",
        });
        return support_.build_json_response(
            403,
            json{{"status", "forbidden"},
                 {"message", "user is not allowed for secured plane"}},
            {});
      }
      PendingSshChallenge challenge;
      challenge.challenge_id = naim::RandomTokenBase64(24);
      challenge.secured_connection = true;
      challenge.secured_user_id = secured_user->id;
      challenge.username = secured_user->name;
      challenge.plane_name = plane_name;
      challenge.fingerprint = fingerprint;
      challenge.challenge_token = naim::RandomTokenBase64(24);
      challenge.expires_at =
          support_.sql_timestamp_after_seconds(SshChallengeLifetimeSeconds());
      challenge.message = support_.build_ssh_challenge_message(
          challenge.username,
          challenge.plane_name,
          challenge.challenge_token,
          challenge.expires_at);
      support_.save_pending_ssh_challenge(challenge);
      store.InsertSecuredConnectionAuthLog({
          0,
          plane_name,
          secured_user->id,
          secured_user->name,
          fingerprint,
          "challenge",
          "accepted",
          RequestRemoteAddress(request),
          "",
          "",
          "{}",
      });
      return support_.build_json_response(
          200,
          json{{"challenge_id", challenge.challenge_id},
               {"challenge_token", challenge.challenge_token},
               {"expires_at", challenge.expires_at},
               {"message", challenge.message}},
          {});
    }
    const auto user = store.LoadUserByUsername(username);
    if (!user.has_value()) {
      return support_.build_json_response(
          404, json{{"status", "not_found"}, {"message", "user not found"}}, {});
    }
    const auto ssh_key =
        store.LoadActiveUserSshKeyByFingerprint(user->id, fingerprint);
    if (!ssh_key.has_value()) {
      return support_.build_json_response(
          404,
          json{{"status", "not_found"},
               {"message", "SSH key fingerprint not found for user"}},
          {});
    }
    PendingSshChallenge challenge;
    challenge.challenge_id = naim::RandomTokenBase64(24);
    challenge.user_id = user->id;
    challenge.ssh_key_id = ssh_key->id;
    challenge.username = user->username;
    challenge.plane_name = plane_name;
    challenge.fingerprint = fingerprint;
    challenge.challenge_token = naim::RandomTokenBase64(24);
    challenge.expires_at =
        support_.sql_timestamp_after_seconds(SshChallengeLifetimeSeconds());
    challenge.message = support_.build_ssh_challenge_message(
        challenge.username,
        challenge.plane_name,
        challenge.challenge_token,
        challenge.expires_at);
    support_.save_pending_ssh_challenge(challenge);
    return support_.build_json_response(
        200,
        json{{"challenge_id", challenge.challenge_id},
             {"challenge_token", challenge.challenge_token},
             {"expires_at", challenge.expires_at},
             {"message", challenge.message}},
        {});
  } catch (const std::exception& error) {
    return support_.build_json_response(
        500,
        json{{"status", "internal_error"},
             {"message", error.what()},
             {"path", request.path}},
        {});
  }
}

HttpResponse AuthHttpService::HandleSshVerify(
    const std::string& db_path,
    const HttpRequest& request) const {
  if (request.method != "POST") {
    return support_.build_json_response(405, json{{"status", "method_not_allowed"}}, {});
  }
  try {
    const json body = ParseJsonBody(request);
    const std::string challenge_id = body.value("challenge_id", std::string{});
    const std::string signature = body.value("signature", std::string{});
    const bool issue_cookie =
        ParseOptionalBooleanField(body, "issue_cookie", false);
    const bool include_token =
        ParseOptionalBooleanField(body, "include_token", !issue_cookie);
    if (challenge_id.empty() || signature.empty()) {
      return support_.build_json_response(
          400,
          json{{"status", "bad_request"},
               {"message", "challenge_id and signature are required"}},
          {});
    }
    const auto challenge = support_.load_pending_ssh_challenge(challenge_id);
    if (!challenge.has_value() ||
        challenge->expires_at < support_.utc_now_sql_timestamp()) {
      return support_.build_json_response(
          410,
          json{{"status", "expired"},
               {"message", "SSH challenge is missing or expired"}},
          {});
    }
    naim::ControllerStore store(db_path);
    store.Initialize();
    if (challenge->secured_connection) {
      if (!IsSecuredRequestTransportAllowed(request)) {
        store.InsertSecuredConnectionAuthLog({
            0,
            challenge->plane_name,
            challenge->secured_user_id,
            challenge->username,
            challenge->fingerprint,
            "verify",
            "rejected",
            RequestRemoteAddress(request),
            "HTTPS/TLS is required for secured connection authentication",
            "",
            "{}",
        });
        return support_.build_json_response(
            403,
            json{{"status", "forbidden"},
                 {"message", "secured connection authentication requires HTTPS/TLS"}},
            {});
      }
      const auto secured_user =
          store.LoadActiveSecuredConnectionUser(challenge->secured_user_id);
      if (!secured_user.has_value()) {
        store.InsertSecuredConnectionAuthLog({
            0,
            challenge->plane_name,
            challenge->secured_user_id,
            challenge->username,
            challenge->fingerprint,
            "verify",
            "rejected",
            RequestRemoteAddress(request),
            "secured connection user not found",
            "",
            "{}",
        });
        return support_.build_json_response(
            404,
            json{{"status", "not_found"},
                 {"message", "secured connection user not found"}},
            {});
      }
      if (!support_.verify_ssh_detached_signature(
              challenge->username,
              secured_user->public_key,
              challenge->message,
              signature)) {
        store.InsertSecuredConnectionAuthLog({
            0,
            challenge->plane_name,
            secured_user->id,
            secured_user->name,
            challenge->fingerprint,
            "verify",
            "rejected",
            RequestRemoteAddress(request),
            "SSH signature verification failed",
            "",
            "{}",
        });
        return support_.build_json_response(
            403,
            json{{"status", "forbidden"},
                 {"message", "SSH signature verification failed"}},
            {});
      }
      const std::string now = support_.utc_now_sql_timestamp();
      store.TouchSecuredConnectionUser(secured_user->id, now);
      const std::string session_token = naim::RandomTokenBase64(32);
      store.InsertSecuredConnectionSession({
          session_token,
          secured_user->id,
          challenge->plane_name,
          support_.sql_timestamp_after_seconds(SshSessionLifetimeSeconds()),
          "",
          "",
          now,
      });
      store.InsertSecuredConnectionAuthLog({
          0,
          challenge->plane_name,
          secured_user->id,
          secured_user->name,
          challenge->fingerprint,
          "verify",
          "accepted",
          RequestRemoteAddress(request),
          "",
          "",
          "{}",
      });
      support_.erase_pending_ssh_challenge(challenge_id);
      json payload{
          {"plane_name", challenge->plane_name},
          {"expires_at",
           support_.sql_timestamp_after_seconds(SshSessionLifetimeSeconds())},
          {"session_kind", "secured_ssh"},
      };
      if (issue_cookie) {
        payload["issued_cookie"] = true;
      }
      if (include_token) {
        payload["token"] = session_token;
      }
      std::map<std::string, std::string> headers;
      if (issue_cookie) {
        headers["Set-Cookie"] =
            support_.session_cookie_header(session_token, request);
      }
      return support_.build_json_response(200, payload, headers);
    }
    const auto ssh_key = store.LoadActiveUserSshKeyById(challenge->ssh_key_id);
    if (!ssh_key.has_value()) {
      return support_.build_json_response(
          404, json{{"status", "not_found"}, {"message", "SSH key not found"}}, {});
    }
    if (!support_.verify_ssh_detached_signature(
            challenge->username,
            ssh_key->public_key,
            challenge->message,
            signature)) {
      return support_.build_json_response(
          403,
          json{{"status", "forbidden"},
               {"message", "SSH signature verification failed"}},
          {});
    }
    store.TouchUserSshKey(ssh_key->id, support_.utc_now_sql_timestamp());
    const std::string session_token = support_.create_controller_session(
        store, challenge->user_id, "ssh", challenge->plane_name);
    support_.erase_pending_ssh_challenge(challenge_id);
    json payload{
        {"plane_name", challenge->plane_name},
        {"expires_at",
         support_.sql_timestamp_after_seconds(SshSessionLifetimeSeconds())},
        {"session_kind", "ssh"},
    };
    if (issue_cookie) {
      payload["issued_cookie"] = true;
    }
    if (include_token) {
      payload["token"] = session_token;
    }
    std::map<std::string, std::string> headers;
    if (issue_cookie) {
      headers["Set-Cookie"] =
          support_.session_cookie_header(session_token, request);
    }
    return support_.build_json_response(
        200,
        payload,
        headers);
  } catch (const std::invalid_argument& error) {
    return support_.build_json_response(
        400,
        json{{"status", "bad_request"},
             {"message", error.what()},
             {"path", request.path}},
        {});
  } catch (const std::exception& error) {
    return support_.build_json_response(
        500,
        json{{"status", "internal_error"},
             {"message", error.what()},
             {"path", request.path}},
        {});
  }
}
