#include "interaction/interaction_runtime_server.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <csignal>
#include <fstream>
#include <map>
#include <optional>
#include <stdexcept>
#include <thread>

#include "http/controller_http_server_support.h"
#include "infra/controller_network_manager.h"
#include "interaction/interaction_completion_policy_support.h"
#include "interaction/interaction_context_compression_service.h"
#include "interaction/interaction_payload_builder.h"
#include "interaction/interaction_runtime_request_codec.h"
#include "interaction/interaction_text_post_processor.h"
#include "naim/state/state_json.h"
#include "naim/runtime/runtime_status.h"

namespace naim::interaction_runtime {

namespace {

std::atomic<bool>* g_stop_requested = nullptr;
constexpr const char* kSkillsSystemInstructionPayloadKey =
    "_naim_skills_system_instruction";
constexpr const char* kAppliedSkillsPayloadKey = "_naim_applied_skills";
constexpr const char* kAutoAppliedSkillsPayloadKey = "_naim_auto_applied_skills";
constexpr const char* kSkillsSessionIdPayloadKey = "_naim_skills_session_id";
constexpr const char* kSkillResolutionModePayloadKey = "_naim_skill_resolution_mode";
constexpr const char* kBrowsingSystemInstructionPayloadKey =
    "_naim_browsing_system_instruction";
constexpr const char* kBrowsingSummaryPayloadKey = "_naim_browsing_summary";
constexpr const char* kWebGatewayContextPayloadKey = "_naim_webgateway_context";
constexpr const char* kWebGatewayPolicyPayloadKey = "_naim_webgateway_policy";
constexpr const char* kWebGatewayReviewPayloadKey = "_naim_webgateway_review";

void SignalHandler(int) {
  if (g_stop_requested != nullptr) {
    g_stop_requested->store(true);
  }
}

std::string LowercaseCopy(std::string value) {
  for (char& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

bool RequestWantsStream(const HttpRequest& request) {
  if (request.path == "/v1/chat/completions/stream") {
    return true;
  }
  try {
    const auto wrapped_request =
        naim::controller::InteractionRuntimeRequestCodec{}.Deserialize(request.body);
    return wrapped_request.force_stream;
  } catch (const std::exception&) {
  }
  try {
    const auto payload = nlohmann::json::parse(request.body);
    return payload.is_object() && payload.value("stream", false);
  } catch (const std::exception&) {
  }
  return false;
}

std::optional<std::string> FindRequestHeader(
    const HttpRequest& request,
    const std::string& key) {
  const auto it = request.headers.find(LowercaseCopy(key));
  if (it == request.headers.end() || it->second.empty()) {
    return std::nullopt;
  }
  return it->second;
}

bool HasExplicitSkillIds(const nlohmann::json& payload) {
  return payload.contains("skill_ids") && payload.at("skill_ids").is_array() &&
         !payload.at("skill_ids").empty();
}

int ParsePositiveIntOr(const std::string& value, int fallback) {
  try {
    const int parsed = std::stoi(value);
    return parsed > 0 ? parsed : fallback;
  } catch (const std::exception&) {
    return fallback;
  }
}

std::string BuildSkillsSystemInstruction(const nlohmann::json& skills) {
  std::string instruction = "Skills currently applied for this request:";
  for (const auto& skill : skills) {
    if (!skill.is_object()) {
      continue;
    }
    const std::string name = skill.value("name", std::string{});
    const std::string description = skill.value("description", std::string{});
    const std::string content = skill.value("content", std::string{});
    if (content.empty()) {
      continue;
    }
    instruction += "\n\nSkill: " + name;
    if (!description.empty()) {
      instruction += "\nDescription: " + description;
    }
    instruction += "\n\nInstructions:\n" + content;
  }
  return instruction;
}

std::string ReadJsonStringOrDefault(
    const nlohmann::json& payload,
    const std::string& key,
    std::string default_value = {}) {
  const auto it = payload.find(key);
  if (it == payload.end() || it->is_null() || !it->is_string()) {
    return default_value;
  }
  return it->get<std::string>();
}

std::string LastUserMessageContent(
    const naim::controller::InteractionRequestContext& context) {
  if (!context.payload.contains("messages") ||
      !context.payload.at("messages").is_array()) {
    return "";
  }
  const auto& messages = context.payload.at("messages");
  for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
    if (it->is_object() && it->value("role", std::string{}) == "user" &&
        it->contains("content") && it->at("content").is_string()) {
      return it->at("content").get<std::string>();
    }
  }
  return "";
}

nlohmann::json ConversationSlice(
    const naim::controller::InteractionRequestContext& context) {
  if (context.payload.contains("messages") &&
      context.payload.at("messages").is_array()) {
    return context.payload.at("messages");
  }
  return nlohmann::json::array();
}

std::string ReadPersistedBrowsingMode(
    const naim::controller::InteractionRequestContext& context) {
  const nlohmann::json& state =
      context.payload.contains(naim::controller::kInteractionSessionContextStatePayloadKey) &&
              context.payload.at(naim::controller::kInteractionSessionContextStatePayloadKey)
                  .is_object()
          ? context.payload.at(naim::controller::kInteractionSessionContextStatePayloadKey)
          : context.session_context_state;
  if (!state.is_object()) {
    return "auto";
  }
  const std::string mode = state.value("browsing_mode", std::string{});
  if (mode == "enabled" || mode == "disabled") {
    return mode;
  }
  return "auto";
}

nlohmann::json BuildDisabledWebGatewayContext() {
  return nlohmann::json{
      {"mode", "disabled"},
      {"mode_source", "default_off"},
      {"plane_enabled", false},
      {"ready", false},
      {"session_backend", "broker_fallback"},
      {"rendered_browser_enabled", true},
      {"rendered_browser_ready", false},
      {"login_enabled", false},
      {"toggle_only", false},
      {"decision", "disabled"},
      {"reason", "web_mode_disabled"},
      {"lookup_state", "disabled"},
      {"lookup_attempted", false},
      {"lookup_required", false},
      {"evidence_attached", false},
      {"searches", nlohmann::json::array()},
      {"sources", nlohmann::json::array()},
      {"errors", nlohmann::json::array()},
      {"refusal", nullptr},
      {"response_policy", nlohmann::json::object()},
  };
}

nlohmann::json BuildUnavailableWebGatewayContext(
    const std::string& reason,
    const std::string& error_message) {
  nlohmann::json context = BuildDisabledWebGatewayContext();
  context["mode"] = "enabled";
  context["mode_source"] = "webgateway_unreachable";
  context["plane_enabled"] = true;
  context["decision"] = "unavailable";
  context["reason"] = reason.empty() ? "webgateway_unavailable" : reason;
  context["lookup_state"] = "required_but_unavailable";
  context["lookup_attempted"] = false;
  context["lookup_required"] = true;
  context["response_policy"] = nlohmann::json{
      {"must_disclose_web_unavailable", true},
      {"must_not_suggest_local_access", false},
      {"must_refuse_upload", false},
      {"must_use_only_evidence", false},
      {"must_not_claim_unverified_web_lookup", true},
      {"blocked_reason", nullptr},
      {"unavailable_disclaimer",
       "Web browsing was unavailable for this request, so I could not verify fresh public sources online."},
  };
  if (!error_message.empty()) {
    context["errors"] = nlohmann::json::array(
        {nlohmann::json{{"code", reason.empty() ? "webgateway_unavailable" : reason},
                        {"message", error_message}}});
  }
  return context;
}

void ApplyWebGatewayPayload(
    naim::controller::InteractionRequestContext* context,
    const nlohmann::json& webgateway_context,
    const nlohmann::json& response_policy,
    const std::string& model_instruction,
    const std::optional<std::string>& refusal,
    const std::string& decision) {
  if (context == nullptr) {
    return;
  }
  if (!model_instruction.empty()) {
    context->payload[kBrowsingSystemInstructionPayloadKey] = model_instruction;
  }
  context->payload[kBrowsingSummaryPayloadKey] = webgateway_context;
  context->payload[kWebGatewayContextPayloadKey] = webgateway_context;
  context->payload[kWebGatewayPolicyPayloadKey] = response_policy;
  context->payload[kWebGatewayReviewPayloadKey] =
      nlohmann::json{
          {"decision", decision},
          {"response_policy", response_policy},
          {"refusal", refusal.has_value() ? nlohmann::json(*refusal) : nlohmann::json(nullptr)},
      };
}

std::optional<std::string> ExtractAssistantContent(const nlohmann::json& payload) {
  if (!payload.is_object() || !payload.contains("choices") ||
      !payload.at("choices").is_array() || payload.at("choices").empty() ||
      !payload.at("choices").at(0).is_object()) {
    return std::nullopt;
  }
  const auto& choice = payload.at("choices").at(0);
  if (choice.contains("message") && choice.at("message").is_object() &&
      choice.at("message").contains("content") &&
      choice.at("message").at("content").is_string()) {
    return choice.at("message").at("content").get<std::string>();
  }
  if (choice.contains("text") && choice.at("text").is_string()) {
    return choice.at("text").get<std::string>();
  }
  return std::nullopt;
}

void SetAssistantContent(nlohmann::json* payload, const std::string& content) {
  if (payload == nullptr || !payload->is_object() || !payload->contains("choices") ||
      !payload->at("choices").is_array() || payload->at("choices").empty() ||
      !payload->at("choices").at(0).is_object()) {
    return;
  }
  auto& choice = payload->at("choices").at(0);
  if (choice.contains("message") && choice.at("message").is_object()) {
    choice.at("message")["content"] = content;
    return;
  }
  if (choice.contains("text")) {
    choice["text"] = content;
  }
}

void SetNoResolvedSkills(naim::controller::InteractionRequestContext* context) {
  if (context == nullptr) {
    return;
  }
  context->payload[kAppliedSkillsPayloadKey] = nlohmann::json::array();
  context->payload[kAutoAppliedSkillsPayloadKey] = nlohmann::json::array();
  context->payload[kSkillResolutionModePayloadKey] = "none";
}

std::vector<std::pair<std::string, std::string>> JsonHeaders() {
  return {{"Accept", "application/json"}, {"Content-Type", "application/json"}};
}

std::map<std::string, std::string> BuildCompressionHeaders(
    const naim::controller::InteractionRequestContext& request_context) {
  std::map<std::string, std::string> headers;
  if (!request_context.payload.contains(naim::controller::kInteractionSessionContextStatePayloadKey) ||
      !request_context.payload.at(naim::controller::kInteractionSessionContextStatePayloadKey)
           .is_object()) {
    return headers;
  }
  const auto& context_state =
      request_context.payload.at(naim::controller::kInteractionSessionContextStatePayloadKey);
  if (!context_state.contains("context_compression") ||
      !context_state.at("context_compression").is_object()) {
    return headers;
  }
  const auto& compression = context_state.at("context_compression");
  headers["x-naim-context-compression-enabled"] =
      compression.value("enabled", false) ? "true" : "false";
  headers["x-naim-context-compression-status"] =
      compression.value("status", std::string("none"));
  headers["x-naim-dialog-estimate-before"] =
      std::to_string(compression.value("dialog_estimate_before", 0));
  headers["x-naim-dialog-estimate-after"] =
      std::to_string(compression.value("dialog_estimate_after", 0));
  headers["x-naim-context-compression-ratio"] =
      std::to_string(compression.value("compression_ratio", 1.0));
  return headers;
}

void AddRuntimeTelemetryHeaders(
    HttpResponse* response,
    bool local_raw_execution,
    int skills_resolve_ms,
    int context_compression_ms,
    int prompt_build_ms,
    int local_runtime_ms) {
  if (response == nullptr) {
    return;
  }
  response->headers["x-naim-local-raw-execution"] =
      local_raw_execution ? "true" : "false";
  response->headers["x-naim-skills-resolve-ms"] =
      std::to_string(skills_resolve_ms);
  response->headers["x-naim-context-compression-ms"] =
      std::to_string(context_compression_ms);
  response->headers["x-naim-prompt-build-ms"] = std::to_string(prompt_build_ms);
  response->headers["x-naim-runtime-local-execution-ms"] =
      std::to_string(local_runtime_ms);
}

HttpResponse SanitizeJsonChatResponse(HttpResponse response) {
  if (response.status_code < 200 || response.status_code >= 300 || response.body.empty()) {
    return response;
  }
  nlohmann::json payload = nlohmann::json::parse(response.body, nullptr, false);
  if (payload.is_discarded() || !payload.is_object()) {
    return response;
  }
  const naim::controller::InteractionTextPostProcessor post_processor;
  if (payload.contains("choices") && payload.at("choices").is_array() &&
      !payload.at("choices").empty() && payload.at("choices").at(0).is_object()) {
    auto& first_choice = payload.at("choices").at(0);
    if (first_choice.contains("message") && first_choice.at("message").is_object()) {
      auto& message = first_choice.at("message");
      message["content"] = post_processor.ExtractInteractionText(
          nlohmann::json{{"choices", nlohmann::json::array({first_choice})}});
      response.body = payload.dump();
      return response;
    }
    if (first_choice.contains("text") && first_choice.at("text").is_string()) {
      first_choice["text"] = post_processor.SanitizeInteractionText(
          first_choice.at("text").get<std::string>());
      response.body = payload.dump();
    }
  }
  return response;
}

}  // namespace

InteractionRuntimeServer::InteractionRuntimeServer(InteractionRuntimeConfig config)
    : config_(std::move(config)) {}

InteractionRuntimeServer::~InteractionRuntimeServer() {
  RequestStop();
}

int InteractionRuntimeServer::Run() {
  std::filesystem::create_directories(config_.status_path.parent_path());
  WriteRuntimeStatus("starting", false);
  SetReadyFile(false);

  g_stop_requested = &stop_requested_;
  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);

  listen_fd_ = naim::controller::ControllerNetworkManager::CreateListenSocket(
      config_.listen_host,
      config_.port);
  WriteRuntimeStatus("running", true);
  SetReadyFile(true);

  try {
    AcceptLoop();
  } catch (...) {
    WriteRuntimeStatus("stopped", false);
    SetReadyFile(false);
    naim::controller::ControllerNetworkManager::ShutdownAndCloseSocket(listen_fd_);
    listen_fd_ = naim::platform::kInvalidSocket;
    throw;
  }

  WriteRuntimeStatus("stopped", false);
  SetReadyFile(false);
  naim::controller::ControllerNetworkManager::ShutdownAndCloseSocket(listen_fd_);
  listen_fd_ = naim::platform::kInvalidSocket;
  return 0;
}

void InteractionRuntimeServer::RequestStop() {
  const bool was_requested = stop_requested_.exchange(true);
  if (!was_requested && naim::platform::IsSocketValid(listen_fd_)) {
    WriteRuntimeStatus("stopping", false);
    SetReadyFile(false);
    naim::controller::ControllerNetworkManager::ShutdownAndCloseSocket(listen_fd_);
  }
}

void InteractionRuntimeServer::AcceptLoop() {
  while (!stop_requested_.load()) {
    const auto client_fd = accept(listen_fd_, nullptr, nullptr);
    if (!naim::platform::IsSocketValid(client_fd)) {
      if (stop_requested_.load() || naim::platform::LastSocketErrorWasInterrupted()) {
        continue;
      }
      throw std::runtime_error(
          "accept failed: " + naim::controller::ControllerNetworkManager::SocketErrorMessage());
    }
    std::thread(&InteractionRuntimeServer::HandleClient, this, client_fd).detach();
  }
}

void InteractionRuntimeServer::HandleClient(naim::platform::SocketHandle client_fd) {
  std::string request_data;
  std::array<char, 8192> buffer{};
  std::size_t expected_request_bytes = 0;
  while (true) {
    const ssize_t read_count = recv(client_fd, buffer.data(), buffer.size(), 0);
    if (read_count <= 0) {
      break;
    }
    request_data.append(buffer.data(), static_cast<std::size_t>(read_count));
    if (expected_request_bytes == 0) {
      expected_request_bytes =
          naim::controller::ControllerHttpServerSupport::ExpectedRequestBytes(request_data);
    }
    if (expected_request_bytes != 0 && request_data.size() >= expected_request_bytes) {
      break;
    }
  }

  if (request_data.empty()) {
    naim::controller::ControllerNetworkManager::ShutdownAndCloseSocket(client_fd);
    return;
  }

  try {
    const HttpRequest request =
        naim::controller::ControllerHttpServerSupport::ParseHttpRequest(request_data);
    if (request.method == "POST" &&
        (request.path == "/v1/chat/completions" ||
         request.path == "/v1/chat/completions/stream")) {
      if (RequestWantsStream(request)) {
        ExecuteStream(client_fd, request);
        return;
      }
    }
    naim::controller::ControllerNetworkManager::SendHttpResponse(
        client_fd,
        HandleRequest(request));
  } catch (const std::exception& error) {
    naim::controller::ControllerNetworkManager::SendHttpResponse(
        client_fd,
        BuildJsonResponse(
            500,
            nlohmann::json{
                {"error", "internal_error"},
                {"message", error.what()},
            }));
  }
  naim::controller::ControllerNetworkManager::ShutdownAndCloseSocket(client_fd);
}

HttpResponse InteractionRuntimeServer::HandleRequest(const HttpRequest& request) {
  if (request.method == "GET") {
    return HandleGet(request);
  }
  if (request.method == "POST") {
    return HandlePost(request);
  }
  return BuildJsonResponse(
      405,
      nlohmann::json{{"error", "method_not_allowed"}, {"message", "method not allowed"}});
}

HttpResponse InteractionRuntimeServer::HandleGet(const HttpRequest& request) {
  const auto parts = SplitPath(request.path);
  if (parts.size() == 1 && parts[0] == "health") {
    return BuildJsonResponse(200, nlohmann::json{{"ok", true}, {"ready", true}});
  }
  if (parts.size() == 2 && parts[0] == "v1" && parts[1] == "models") {
    return SendControllerHttpRequest(UpstreamTarget(), "GET", "/models");
  }
  return BuildJsonResponse(
      404,
      nlohmann::json{{"error", "not_found"}, {"message", "route not found"}});
}

HttpResponse InteractionRuntimeServer::HandlePost(const HttpRequest& request) {
  if (request.path == "/v1/chat/completions" ||
      request.path == "/v1/chat/completions/stream") {
    return ExecuteNonStream(request);
  }
  return BuildJsonResponse(
      404,
      nlohmann::json{{"error", "not_found"}, {"message", "route not found"}});
}

HttpResponse InteractionRuntimeServer::ExecuteNonStream(const HttpRequest& request) {
  if (ShouldProxyRawRequestThroughController(request)) {
    return ProxyRawRequestThroughController(
        request,
        "/api/v1/planes/" + config_.plane_name + "/interaction/chat/completions");
  }
  const auto local_started_at = std::chrono::steady_clock::now();
  auto execution = BuildRuntimeExecution(request);
  const auto browsing_started_at = std::chrono::steady_clock::now();
  const int browsing_resolve_ms =
      ResolvePlaneOwnedBrowsing(execution.resolution, &execution.request_context);
  const auto browsing_finished_at = std::chrono::steady_clock::now();
  const auto compression_started_at = std::chrono::steady_clock::now();
  naim::controller::InteractionContextCompressionService().Apply(
      execution.resolution,
      &execution.request_context);
  const auto compression_finished_at = std::chrono::steady_clock::now();
  const auto prompt_started_at = std::chrono::steady_clock::now();
  const std::string upstream_body = naim::controller::BuildInteractionUpstreamBodyPayload(
      execution.resolution,
      execution.request_context.payload,
      execution.force_stream,
      execution.resolved_policy,
      execution.structured_output_json);
  const auto prompt_finished_at = std::chrono::steady_clock::now();
  HttpResponse response = SendControllerHttpRequest(
      UpstreamTarget(),
      "POST",
      "/chat/completions",
      upstream_body,
      {{"Accept", "application/json"}});
  ReviewPlaneOwnedBrowsingResponse(
      execution.resolution,
      execution.request_context,
      &response);
  for (const auto& [name, value] : BuildCompressionHeaders(execution.request_context)) {
    response.headers[name] = value;
  }
  response.headers["x-naim-webgateway-resolve-ms"] =
      std::to_string(browsing_resolve_ms);
  response.headers["x-naim-webgateway-total-ms"] =
      std::to_string(static_cast<int>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              browsing_finished_at - browsing_started_at)
              .count()));
  AddRuntimeTelemetryHeaders(
      &response,
      execution.local_raw_execution,
      execution.skills_resolve_ms,
      static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                           compression_finished_at - compression_started_at)
                           .count()),
      static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                           prompt_finished_at - prompt_started_at)
                           .count()),
      static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                           prompt_finished_at - local_started_at)
                           .count()));
  return SanitizeJsonChatResponse(std::move(response));
}

void InteractionRuntimeServer::ExecuteStream(
    naim::platform::SocketHandle client_fd,
    const HttpRequest& request) {
  if (ShouldProxyRawRequestThroughController(request)) {
    ProxyRawStreamThroughController(
        client_fd,
        request,
        "/api/v1/planes/" + config_.plane_name + "/interaction/chat/completions/stream");
    return;
  }
  const auto local_started_at = std::chrono::steady_clock::now();
  auto execution = BuildRuntimeExecution(request);
  execution.force_stream = true;
  const auto browsing_started_at = std::chrono::steady_clock::now();
  const int browsing_resolve_ms =
      ResolvePlaneOwnedBrowsing(execution.resolution, &execution.request_context);
  const auto browsing_finished_at = std::chrono::steady_clock::now();
  const auto compression_started_at = std::chrono::steady_clock::now();
  naim::controller::InteractionContextCompressionService().Apply(
      execution.resolution,
      &execution.request_context);
  const auto compression_finished_at = std::chrono::steady_clock::now();
  const auto prompt_started_at = std::chrono::steady_clock::now();
  const std::string upstream_body = naim::controller::BuildInteractionUpstreamBodyPayload(
      execution.resolution,
      execution.request_context.payload,
      true,
      execution.resolved_policy,
      execution.structured_output_json);
  const auto prompt_finished_at = std::chrono::steady_clock::now();
  auto upstream = OpenInteractionStreamRequest(
      UpstreamTarget(),
      "interaction-runtime",
      upstream_body,
      "/chat/completions");
  auto response_headers = BuildCompressionHeaders(execution.request_context);
  response_headers["x-naim-local-raw-execution"] =
      execution.local_raw_execution ? "true" : "false";
  response_headers["x-naim-skills-resolve-ms"] =
      std::to_string(execution.skills_resolve_ms);
  response_headers["x-naim-webgateway-resolve-ms"] =
      std::to_string(browsing_resolve_ms);
  response_headers["x-naim-webgateway-total-ms"] =
      std::to_string(static_cast<int>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              browsing_finished_at - browsing_started_at)
              .count()));
  response_headers["x-naim-context-compression-ms"] =
      std::to_string(static_cast<int>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              compression_finished_at - compression_started_at)
              .count()));
  response_headers["x-naim-prompt-build-ms"] =
      std::to_string(static_cast<int>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              prompt_finished_at - prompt_started_at)
              .count()));
  response_headers["x-naim-runtime-local-execution-ms"] =
      std::to_string(static_cast<int>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              prompt_finished_at - local_started_at)
              .count()));
  if (!naim::controller::ControllerNetworkManager::SendSseHeaders(
          client_fd,
          response_headers)) {
    upstream.close();
    naim::controller::ControllerNetworkManager::ShutdownAndCloseSocket(client_fd);
    return;
  }
  if (!upstream.initial_body.empty() &&
      !naim::controller::ControllerNetworkManager::SendAll(client_fd, upstream.initial_body)) {
    upstream.close();
    naim::controller::ControllerNetworkManager::ShutdownAndCloseSocket(client_fd);
    return;
  }
  while (true) {
    const std::string chunk = upstream.read_next_chunk();
    if (chunk.empty()) {
      break;
    }
    if (!naim::controller::ControllerNetworkManager::SendAll(client_fd, chunk)) {
      break;
    }
  }
  upstream.close();
  naim::controller::ControllerNetworkManager::ShutdownAndCloseSocket(client_fd);
}

bool InteractionRuntimeServer::ShouldProxyRawRequestThroughController(
    const HttpRequest& request) const {
  if (TryBuildWrappedRuntimeExecution(request.body).has_value()) {
    return false;
  }
  if (HasLocalPlaneStateSnapshot()) {
    return false;
  }
  return !config_.controller_url.empty();
}

bool InteractionRuntimeServer::HasLocalPlaneStateSnapshot() const {
  try {
    const auto snapshot = LoadPlaneStatePayloadFromSnapshot();
    return snapshot.is_object() && snapshot.contains("desired_state") &&
           snapshot.at("desired_state").is_object();
  } catch (const std::exception&) {
    return false;
  }
}

HttpResponse InteractionRuntimeServer::ProxyRawRequestThroughController(
    const HttpRequest& request,
    const std::string& controller_path) const {
  const auto started_at = std::chrono::steady_clock::now();
  std::vector<std::pair<std::string, std::string>> headers{
      {"Accept", "application/json"},
      {"Content-Type", "application/json"},
  };
  if (const auto token = FindRequestHeader(request, "x-naim-session-token");
      token.has_value()) {
    headers.emplace_back("X-Naim-Session-Token", *token);
  }
  if (const auto request_id = FindRequestHeader(request, "x-naim-request-id");
      request_id.has_value()) {
    headers.emplace_back("X-Naim-Request-Id", *request_id);
  }
  HttpResponse response = SendControllerHttpRequest(
      ParseControllerEndpointTarget(config_.controller_url),
      "POST",
      controller_path,
      request.body,
      headers);
  response.headers["x-naim-local-raw-execution"] = "false";
  response.headers["x-naim-controller-proxy-fallback-ms"] =
      std::to_string(static_cast<int>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - started_at)
              .count()));
  return response;
}

void InteractionRuntimeServer::ProxyRawStreamThroughController(
    naim::platform::SocketHandle client_fd,
    const HttpRequest& request,
    const std::string& controller_path) const {
  std::vector<std::pair<std::string, std::string>> headers{
      {"Accept", "text/event-stream"},
      {"Content-Type", "application/json"},
  };
  const std::string request_id =
      FindRequestHeader(request, "x-naim-request-id").value_or("interaction-runtime");
  if (const auto token = FindRequestHeader(request, "x-naim-session-token");
      token.has_value()) {
    headers.emplace_back("X-Naim-Session-Token", *token);
  }
  auto upstream = OpenInteractionStreamRequest(
      ParseControllerEndpointTarget(config_.controller_url),
      request_id,
      request.body,
      controller_path,
      headers);
  if (!naim::controller::ControllerNetworkManager::SendSseHeaders(client_fd, {})) {
    upstream.close();
    naim::controller::ControllerNetworkManager::ShutdownAndCloseSocket(client_fd);
    return;
  }
  if (!upstream.initial_body.empty() &&
      !naim::controller::ControllerNetworkManager::SendAll(client_fd, upstream.initial_body)) {
    upstream.close();
    naim::controller::ControllerNetworkManager::ShutdownAndCloseSocket(client_fd);
    return;
  }
  while (true) {
    const std::string chunk = upstream.read_next_chunk();
    if (chunk.empty()) {
      break;
    }
    if (!naim::controller::ControllerNetworkManager::SendAll(client_fd, chunk)) {
      break;
    }
  }
  upstream.close();
  naim::controller::ControllerNetworkManager::ShutdownAndCloseSocket(client_fd);
}

InteractionRuntimeServer::RuntimeExecution
InteractionRuntimeServer::BuildRuntimeExecution(const HttpRequest& request) const {
  if (const auto wrapped = TryBuildWrappedRuntimeExecution(request.body);
      wrapped.has_value()) {
    return *wrapped;
  }
  return BuildDirectRuntimeExecution(request);
}

std::optional<InteractionRuntimeServer::RuntimeExecution>
InteractionRuntimeServer::TryBuildWrappedRuntimeExecution(
    const std::string& body) const {
  if (body.empty()) {
    return std::nullopt;
  }
  try {
    const auto request =
        naim::controller::InteractionRuntimeRequestCodec{}.Deserialize(body);
    RuntimeExecution execution;
    execution.resolution.desired_state = request.desired_state;
    execution.resolution.status_payload = request.status_payload;
    execution.request_context.original_payload = request.payload;
    execution.request_context.payload = request.payload;
    execution.request_context.structured_output_json = request.structured_output_json;
    execution.resolved_policy = request.resolved_policy;
    execution.force_stream = request.force_stream;
    execution.structured_output_json = request.structured_output_json;
    return std::optional<RuntimeExecution>(std::move(execution));
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

InteractionRuntimeServer::RuntimeExecution
InteractionRuntimeServer::BuildDirectRuntimeExecution(const HttpRequest& request) const {
  const nlohmann::json state_payload = LoadPlaneStatePayload();
  if (!state_payload.contains("desired_state") ||
      !state_payload.at("desired_state").is_object()) {
    throw std::runtime_error(
        "interaction-runtime plane state payload is missing desired_state");
  }

  const nlohmann::json payload =
      request.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(request.body);
  if (!payload.is_object()) {
    throw std::runtime_error("interaction-runtime request body must be a JSON object");
  }

  RuntimeExecution execution;
  execution.resolution.desired_state =
      naim::DeserializeDesiredStateJson(state_payload.at("desired_state").dump());
  execution.resolution.status_payload = state_payload;
  execution.request_context.original_payload = payload;
  execution.request_context.payload = payload;
  execution.request_context.structured_output_json =
      payload.contains("response_format") && payload.at("response_format").is_object();
  execution.resolved_policy =
      naim::controller::InteractionCompletionPolicySupport{}.ResolvePolicy(
          execution.resolution.desired_state,
          execution.request_context.payload);
  execution.force_stream =
      request.path == "/v1/chat/completions/stream" || payload.value("stream", false);
  execution.structured_output_json =
      execution.request_context.structured_output_json;
  execution.local_raw_execution = true;
  execution.skills_resolve_ms = ResolveExplicitSkillsForDirectExecution(
      execution.resolution,
      &execution.request_context);
  return execution;
}

int InteractionRuntimeServer::ResolveExplicitSkillsForDirectExecution(
    const naim::controller::PlaneInteractionResolution& resolution,
    naim::controller::InteractionRequestContext* request_context) const {
  const auto started_at = std::chrono::steady_clock::now();
  if (request_context == nullptr) {
    throw std::invalid_argument("interaction request context is required");
  }
  if (!HasExplicitSkillIds(request_context->payload)) {
    SetNoResolvedSkills(request_context);
    return 0;
  }
  if (!resolution.desired_state.skills.has_value() ||
      !resolution.desired_state.skills->enabled) {
    throw std::runtime_error("skills are not enabled for this plane");
  }
  const auto target = ResolvePlaneNetworkSkillsTarget(resolution.desired_state);
  if (!target.has_value()) {
    throw std::runtime_error("skills service is not ready for this plane");
  }

  nlohmann::json resolve_payload = nlohmann::json::object();
  resolve_payload["skill_ids"] = request_context->payload.at("skill_ids");
  if (request_context->payload.contains("session_id") &&
      request_context->payload.at("session_id").is_string()) {
    resolve_payload["session_id"] = request_context->payload.at("session_id");
  }

  const HttpResponse response = SendControllerHttpRequest(
      *target,
      "POST",
      "/v1/skills/resolve",
      resolve_payload.dump(),
      JsonHeaders());
  if (response.status_code != 200) {
    throw std::runtime_error(
        "skills service returned status " + std::to_string(response.status_code));
  }
  const nlohmann::json response_payload =
      response.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(response.body);
  const nlohmann::json resolved_skills =
      response_payload.value("skills", nlohmann::json::array());
  if (!resolved_skills.is_array()) {
    throw std::runtime_error("skills service returned malformed resolve payload");
  }

  nlohmann::json applied_skills = nlohmann::json::array();
  for (const auto& skill : resolved_skills) {
    if (!skill.is_object()) {
      continue;
    }
    applied_skills.push_back(
        nlohmann::json{{"id", skill.value("id", std::string{})},
                       {"name", skill.value("name", std::string{})},
                       {"source", skill.value("source", std::string{})}});
  }
  if (!resolved_skills.empty()) {
    request_context->payload[kSkillsSystemInstructionPayloadKey] =
        BuildSkillsSystemInstruction(resolved_skills);
  }
  request_context->payload[kAppliedSkillsPayloadKey] = applied_skills;
  request_context->payload[kAutoAppliedSkillsPayloadKey] = nlohmann::json::array();
  request_context->payload[kSkillResolutionModePayloadKey] = "explicit";
  if (response_payload.contains("skills_session_id") &&
      !response_payload.at("skills_session_id").is_null()) {
    request_context->payload[kSkillsSessionIdPayloadKey] =
        response_payload.at("skills_session_id");
  } else if (request_context->payload.contains("session_id") &&
             request_context->payload.at("session_id").is_string()) {
    request_context->payload[kSkillsSessionIdPayloadKey] =
        request_context->payload.at("session_id");
  }

  return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - started_at)
                              .count());
}

std::optional<naim::controller::ControllerEndpointTarget>
InteractionRuntimeServer::ResolvePlaneNetworkSkillsTarget(
    const naim::DesiredState& desired_state) const {
  const auto it = std::find_if(
      desired_state.instances.begin(),
      desired_state.instances.end(),
      [](const naim::InstanceSpec& instance) {
        return instance.role == naim::InstanceRole::Skills;
      });
  if (it == desired_state.instances.end() || it->name.empty()) {
    return std::nullopt;
  }

  int port = 18120;
  if (const auto env_it = it->environment.find("NAIM_SKILLS_PORT");
      env_it != it->environment.end()) {
    port = ParsePositiveIntOr(env_it->second, port);
  }
  for (const auto& published : it->published_ports) {
    if (published.container_port > 0) {
      port = published.container_port;
      break;
    }
  }
  naim::controller::ControllerEndpointTarget target;
  target.host = it->name;
  target.port = port;
  target.raw = "http://" + target.host + ":" + std::to_string(target.port);
  target.node_name = it->node_name;
  target.route_mode = "plane-network";
  target.route_via_hostd_proxy = false;
  return target;
}

int InteractionRuntimeServer::ResolvePlaneOwnedBrowsing(
    const naim::controller::PlaneInteractionResolution& resolution,
    naim::controller::InteractionRequestContext* request_context) const {
  const auto started_at = std::chrono::steady_clock::now();
  if (request_context == nullptr) {
    throw std::invalid_argument("interaction request context is required");
  }
  if (!resolution.desired_state.browsing.has_value() ||
      !resolution.desired_state.browsing->enabled) {
    ApplyWebGatewayPayload(
        request_context,
        BuildDisabledWebGatewayContext(),
        nlohmann::json::object(),
        "",
        std::nullopt,
        "disabled");
    return 0;
  }

  const auto target = ResolvePlaneNetworkWebGatewayTarget(resolution.desired_state);
  if (!target.has_value()) {
    const nlohmann::json context =
        BuildUnavailableWebGatewayContext("webgateway_target_missing", "");
    ApplyWebGatewayPayload(
        request_context,
        context,
        context.value("response_policy", nlohmann::json::object()),
        "WebGateway could not provide usable evidence for this request. If online verification matters, state that web browsing was unavailable.",
        std::nullopt,
        "unavailable");
    return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started_at)
                                .count());
  }

  nlohmann::json resolve_payload = {
      {"plane_name", resolution.desired_state.plane_name},
      {"conversation_slice", ConversationSlice(*request_context)},
      {"latest_user_message", LastUserMessageContent(*request_context)},
      {"web_mode", ReadPersistedBrowsingMode(*request_context)},
      {"plane_policy",
       nlohmann::json{
           {"enabled", true},
           {"browser_session_enabled",
            resolution.desired_state.browsing->policy.has_value()
                ? nlohmann::json(
                      resolution.desired_state.browsing->policy->browser_session_enabled)
                : nlohmann::json(false)},
           {"rendered_browser_enabled",
            resolution.desired_state.browsing->policy.has_value()
                ? nlohmann::json(
                      resolution.desired_state.browsing->policy->rendered_browser_enabled)
                : nlohmann::json(true)},
       }},
  };

  try {
    const HttpResponse response = SendControllerHttpRequest(
        *target,
        "POST",
        "/resolve",
        resolve_payload.dump(),
        JsonHeaders());
    if (response.status_code != 200) {
      const nlohmann::json context = BuildUnavailableWebGatewayContext(
          "webgateway_upstream_failed",
          "WebGateway returned status " + std::to_string(response.status_code));
      ApplyWebGatewayPayload(
          request_context,
          context,
          context.value("response_policy", nlohmann::json::object()),
          "WebGateway could not provide usable evidence for this request. If online verification matters, state that web browsing was unavailable.",
          std::nullopt,
          "unavailable");
    } else {
      const nlohmann::json payload =
          response.body.empty() ? nlohmann::json::object()
                                : nlohmann::json::parse(response.body, nullptr, false);
      if (!payload.is_object()) {
        throw std::runtime_error("WebGateway returned malformed resolve payload");
      }
      const nlohmann::json webgateway_context =
          payload.contains("context") && payload.at("context").is_object()
              ? payload.at("context")
              : BuildDisabledWebGatewayContext();
      const nlohmann::json response_policy =
          payload.contains("response_policy") && payload.at("response_policy").is_object()
              ? payload.at("response_policy")
              : webgateway_context.value("response_policy", nlohmann::json::object());
      const auto refusal_it = payload.find("refusal");
      std::optional<std::string> refusal = std::nullopt;
      if (refusal_it != payload.end() && refusal_it->is_string()) {
        refusal = refusal_it->get<std::string>();
      }
      ApplyWebGatewayPayload(
          request_context,
          webgateway_context,
          response_policy,
          ReadJsonStringOrDefault(payload, "model_instruction"),
          refusal,
          ReadJsonStringOrDefault(payload, "decision", "disabled"));
    }
  } catch (const std::exception& error) {
    const nlohmann::json context =
        BuildUnavailableWebGatewayContext("webgateway_unavailable", error.what());
    ApplyWebGatewayPayload(
        request_context,
        context,
        context.value("response_policy", nlohmann::json::object()),
        "WebGateway could not provide usable evidence for this request. If online verification matters, state that web browsing was unavailable.",
        std::nullopt,
        "unavailable");
  }
  return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - started_at)
                              .count());
}

void InteractionRuntimeServer::ReviewPlaneOwnedBrowsingResponse(
    const naim::controller::PlaneInteractionResolution& resolution,
    const naim::controller::InteractionRequestContext& request_context,
    HttpResponse* response) const {
  if (response == nullptr || response->status_code != 200 || response->body.empty() ||
      !request_context.payload.contains(kWebGatewayReviewPayloadKey) ||
      !request_context.payload.at(kWebGatewayReviewPayloadKey).is_object() ||
      !resolution.desired_state.browsing.has_value() ||
      !resolution.desired_state.browsing->enabled) {
    return;
  }
  nlohmann::json body = nlohmann::json::parse(response->body, nullptr, false);
  if (!body.is_object()) {
    return;
  }
  const auto draft = ExtractAssistantContent(body);
  if (!draft.has_value()) {
    return;
  }
  const auto target = ResolvePlaneNetworkWebGatewayTarget(resolution.desired_state);
  if (!target.has_value()) {
    body[kWebGatewayContextPayloadKey] =
        request_context.payload.value(kWebGatewayContextPayloadKey, nlohmann::json::object());
    body["webgateway"] = body[kWebGatewayContextPayloadKey];
    response->body = body.dump();
    return;
  }

  nlohmann::json review_payload =
      request_context.payload.at(kWebGatewayReviewPayloadKey);
  review_payload["draft_model_answer"] = *draft;
  try {
    const HttpResponse review_response = SendControllerHttpRequest(
        *target,
        "POST",
        "/review-response",
        review_payload.dump(),
        JsonHeaders());
    if (review_response.status_code == 200 && !review_response.body.empty()) {
      const nlohmann::json review =
          nlohmann::json::parse(review_response.body, nullptr, false);
      if (review.is_object() && review.contains("corrected_answer") &&
          review.at("corrected_answer").is_string()) {
        SetAssistantContent(&body, review.at("corrected_answer").get<std::string>());
      }
      body["_naim_webgateway_review_result"] = review;
    }
  } catch (const std::exception&) {
  }
  body[kWebGatewayContextPayloadKey] =
      request_context.payload.value(kWebGatewayContextPayloadKey, nlohmann::json::object());
  body[kWebGatewayPolicyPayloadKey] =
      request_context.payload.value(kWebGatewayPolicyPayloadKey, nlohmann::json::object());
  body["webgateway"] = body[kWebGatewayContextPayloadKey];
  response->body = body.dump();
}

std::optional<naim::controller::ControllerEndpointTarget>
InteractionRuntimeServer::ResolvePlaneNetworkWebGatewayTarget(
    const naim::DesiredState& desired_state) const {
  if (!config_.webgateway_base_url.empty()) {
    auto target = ParseControllerEndpointTarget(config_.webgateway_base_url);
    target.route_mode = "plane-network";
    target.route_via_hostd_proxy = false;
    return target;
  }
  const auto it = std::find_if(
      desired_state.instances.begin(),
      desired_state.instances.end(),
      [](const naim::InstanceSpec& instance) {
        return instance.role == naim::InstanceRole::Browsing;
      });
  if (it == desired_state.instances.end() || it->name.empty()) {
    return std::nullopt;
  }

  int port = 18130;
  if (const auto env_it = it->environment.find("NAIM_WEBGATEWAY_PORT");
      env_it != it->environment.end()) {
    port = ParsePositiveIntOr(env_it->second, port);
  }
  for (const auto& published : it->published_ports) {
    if (published.container_port > 0) {
      port = published.container_port;
      break;
    }
  }
  naim::controller::ControllerEndpointTarget target;
  target.host = it->name;
  target.port = port;
  target.base_path = "/v1/webgateway";
  target.raw = "http://" + target.host + ":" + std::to_string(target.port) +
               target.base_path;
  target.node_name = it->node_name;
  target.route_mode = "plane-network";
  target.route_via_hostd_proxy = false;
  return target;
}

nlohmann::json InteractionRuntimeServer::LoadPlaneStatePayload() const {
  if (const auto snapshot = LoadPlaneStatePayloadFromSnapshot(); snapshot.is_object()) {
    return snapshot;
  }
  if (config_.controller_url.empty()) {
    throw std::runtime_error("interaction-runtime controller_url is not configured");
  }
  const auto target = ParseControllerEndpointTarget(config_.controller_url);
  const auto response = SendControllerHttpRequest(
      target,
      "GET",
      "/api/v1/planes/" + config_.plane_name);
  if (response.status_code < 200 || response.status_code >= 300) {
    throw std::runtime_error(
        "interaction-runtime failed to load plane state for '" + config_.plane_name +
        "' from controller");
  }
  const auto payload =
      response.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(response.body);
  if (!payload.is_object()) {
    throw std::runtime_error("interaction-runtime plane state response must be a JSON object");
  }
  return payload;
}

nlohmann::json InteractionRuntimeServer::LoadPlaneStatePayloadFromSnapshot() const {
  if (config_.control_root.empty()) {
    return nlohmann::json();
  }
  const std::filesystem::path snapshot_path =
      std::filesystem::path(config_.control_root) / "desired-state.v2.json";
  if (!std::filesystem::exists(snapshot_path)) {
    return nlohmann::json();
  }
  std::ifstream input(snapshot_path);
  if (!input.is_open()) {
    throw std::runtime_error(
        "interaction-runtime failed to open local desired-state snapshot: " +
        snapshot_path.string());
  }
  const std::string state_json{
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>()};
  if (state_json.empty()) {
    throw std::runtime_error(
        "interaction-runtime local desired-state snapshot is empty: " +
        snapshot_path.string());
  }
  const auto desired_state = naim::DeserializeDesiredStateJson(state_json);
  return nlohmann::json{
      {"plane_name", desired_state.plane_name},
      {"desired_state", nlohmann::json::parse(state_json)},
      {"ready", true},
      {"interaction_enabled", true},
      {"status", "ok"},
      {"reason", "local_desired_state_snapshot"},
      {"active_model_id",
       desired_state.bootstrap_model.has_value()
           ? nlohmann::json(desired_state.bootstrap_model->model_id)
           : nlohmann::json(nullptr)},
      {"served_model_name",
       desired_state.bootstrap_model.has_value()
           ? nlohmann::json(
                 desired_state.bootstrap_model->served_model_name.value_or(
                     desired_state.bootstrap_model->model_id))
           : nlohmann::json(nullptr)},
  };
}

HttpResponse InteractionRuntimeServer::BuildJsonResponse(
    int status_code,
    const nlohmann::json& payload) const {
  HttpResponse response;
  response.status_code = status_code;
  response.content_type = "application/json";
  response.body = payload.dump();
  return response;
}

naim::controller::ControllerEndpointTarget InteractionRuntimeServer::UpstreamTarget() const {
  return ParseControllerEndpointTarget(config_.upstream_base);
}

std::vector<std::string> InteractionRuntimeServer::SplitPath(const std::string& path) const {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start < path.size()) {
    while (start < path.size() && path[start] == '/') {
      ++start;
    }
    if (start >= path.size()) {
      break;
    }
    const std::size_t end = path.find('/', start);
    parts.push_back(path.substr(start, end == std::string::npos ? std::string::npos : end - start));
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return parts;
}

void InteractionRuntimeServer::WriteRuntimeStatus(
    const std::string& phase,
    bool ready) const {
  naim::RuntimeStatus status;
  status.plane_name = config_.plane_name;
  status.control_root = config_.control_root;
  status.controller_url = config_.controller_url;
  status.instance_name = config_.instance_name;
  status.instance_role = config_.instance_role;
  status.node_name = config_.node_name;
  status.runtime_backend = "interaction-runtime";
  status.runtime_phase = phase;
  status.gateway_listen = config_.listen_host + ":" + std::to_string(config_.port);
  status.gateway_health_url =
      "http://127.0.0.1:" + std::to_string(config_.port) + "/health";
  status.upstream_models_url = config_.upstream_base + "/models";
  status.ready = ready;
  status.gateway_ready = ready;
  status.inference_ready = ready;
  status.launch_ready = ready;
  status.active_model_ready = ready;
  naim::SaveRuntimeStatusJson(status, config_.status_path.string());
}

void InteractionRuntimeServer::SetReadyFile(bool ready) const {
  const std::filesystem::path ready_path("/tmp/naim-ready");
  std::error_code error;
  if (ready) {
    std::filesystem::create_directories(ready_path.parent_path(), error);
    std::ofstream ready_file(ready_path.string());
    ready_file << '\n';
  } else {
    std::filesystem::remove(ready_path, error);
  }
}

}  // namespace naim::interaction_runtime
