#include "app/controller_app.h"
#include "app/controller_main_includes.h"

namespace {

using nlohmann::json;
using SocketHandle = comet::platform::SocketHandle;

using ControllerCli = comet::controller::ControllerCli;
using ControllerCommandLine = comet::controller::ControllerCommandLine;
using ControllerEndpointTarget = comet::controller::ControllerEndpointTarget;
using ControllerHttpServer = comet::controller::ControllerHttpServer;
using ControllerHttpRouter = comet::controller::ControllerHttpRouter;
using ControllerHttpServerSupport = comet::controller::ControllerHttpServerSupport;
using ControllerNetworkManager = comet::controller::ControllerNetworkManager;

using HostRegistryService = comet::controller::HostRegistryService;
using InteractionCompletionPolicy = comet::controller::InteractionCompletionPolicy;

using PlaneService = comet::controller::PlaneService;
using PlaneInteractionResolution = comet::controller::PlaneInteractionResolution;

using ResolvedInteractionPolicy = comet::controller::ResolvedInteractionPolicy;
using SchedulerService = comet::controller::SchedulerService;
using WebUiComposeMode = comet::controller::WebUiComposeMode;
using WebUiService = comet::controller::WebUiService;

std::string DefaultDbPath() {
  return (std::filesystem::path("var") / "controller.sqlite").string();
}

std::string DefaultArtifactsRoot() {
  return (std::filesystem::path("var") / "artifacts").string();
}

int DefaultStaleAfterSeconds() {
  return 300;
}

int MinimumSafeDirectRebalanceScore() {
  return 100;
}

int MaximumRebalanceIterationsPerGeneration() {
  return 1;
}

int WorkerMinimumResidencySeconds() {
  return 300;
}

int NodeCooldownAfterMoveSeconds() {
  return 60;
}

int VerificationStableSamplesRequired() {
  return 3;
}

int VerificationTimeoutSeconds() {
  return 45;
}

thread_local const HttpRequest* g_current_http_request = nullptr;

struct ScopedCurrentHttpRequest {
  const HttpRequest* previous = nullptr;

  explicit ScopedCurrentHttpRequest(const HttpRequest& request) : previous(g_current_http_request) {
    g_current_http_request = &request;
  }

  ~ScopedCurrentHttpRequest() {
    g_current_http_request = previous;
  }
};

std::vector<comet::HostObservation> FilterHostObservationsForPlane(
    const std::vector<comet::HostObservation>& observations,
    const std::string& plane_name);

bool ObservationMatchesPlane(
    const comet::HostObservation& observation,
    const std::string& plane_name);

std::string SerializeEventPayload(const json& payload);

std::optional<long long> HeartbeatAgeSeconds(const std::string& heartbeat_at);

std::vector<comet::RuntimeProcessStatus> ParseInstanceRuntimeStatuses(
    const comet::HostObservation& observation);

std::string UtcNowSqlTimestamp();

void AppendControllerEvent(
    comet::ControllerStore& store,
    const std::string& category,
    const std::string& event_type,
    const std::string& message,
    const json& payload = json::object(),
    const std::string& plane_name = "",
    const std::string& node_name = "",
    const std::string& worker_name = "",
    const std::optional<int>& assignment_id = std::nullopt,
    const std::optional<int>& rollout_action_id = std::nullopt,
    const std::string& severity = "info");

std::string SqlTimestampAfterSeconds(int seconds);

std::optional<long long> TimestampAgeSeconds(const std::string& timestamp_text);

std::string Trim(const std::string& value);

std::string NormalizeLanguageCode(const std::string& value);

std::optional<std::string> FindQueryString(
    const HttpRequest& request,
    const std::string& key);

std::optional<int> FindQueryInt(
    const HttpRequest& request,
    const std::string& key);

HttpResponse BuildJsonResponse(
    int status_code,
    const json& payload,
    const std::map<std::string, std::string>& headers = {});

SchedulerRuntimeView LoadSchedulerRuntimeView(
    comet::ControllerStore& store,
    const std::optional<comet::DesiredState>& desired_state);

SchedulerService MakeSchedulerService(
    const std::string& db_path,
    const std::string& artifacts_root);
comet::controller::BundleCliService MakeBundleCliService();
comet::controller::ControllerPrintService MakeControllerPrintService();
comet::controller::ControllerRuntimeSupportService MakeControllerRuntimeSupportService();
comet::controller::DesiredStatePolicyService MakeDesiredStatePolicyService();
comet::controller::PlaneRealizationService MakePlaneRealizationService();
comet::controller::ReadModelService MakeReadModelService();

comet::controller::SchedulerDomainService MakeSchedulerDomainService();
comet::controller::StateAggregateLoader MakeStateAggregateLoader(
    const comet::controller::SchedulerDomainService& scheduler_domain_service,
    const SchedulerViewService& scheduler_view_service);
comet::controller::PlaneMutationService MakePlaneMutationService();
comet::controller::AssignmentOrchestrationService MakeAssignmentOrchestrationService();

PlaneService MakePlaneService(const std::string& db_path);

std::optional<comet::RolloutActionRecord> FindRolloutActionById(
    const std::vector<comet::RolloutActionRecord>& actions,
    int action_id);

int VerificationStableSamplesRequired();

bool CanFinalizeDeletedPlane(
    comet::ControllerStore& store,
    const std::string& plane_name);

std::string FormatDisplayTimestamp(const std::string& value);

comet::controller::ControllerRuntimeSupportService MakeControllerRuntimeSupportService() {
  return comet::controller::ControllerRuntimeSupportService{};
}

comet::controller::DesiredStatePolicyService MakeDesiredStatePolicyService() {
  return comet::controller::DesiredStatePolicyService{};
}

comet::controller::PlaneRealizationService MakePlaneRealizationService() {
  static const comet::controller::ControllerRuntimeSupportService runtime_support_service =
      MakeControllerRuntimeSupportService();
  return comet::controller::PlaneRealizationService(
      &runtime_support_service,
      DefaultStaleAfterSeconds());
}

std::string Trim(const std::string& value) {
  std::size_t start = 0;
  while (start < value.size() &&
         std::isspace(static_cast<unsigned char>(value[start])) != 0) {
    ++start;
  }
  std::size_t end = value.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }
  return value.substr(start, end - start);
}

std::string Lowercase(std::string value) {
  std::transform(
      value.begin(),
      value.end(),
      value.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

std::string NormalizeLanguageCode(const std::string& value) {
  std::string normalized;
  normalized.reserve(value.size());
  for (unsigned char ch : value) {
    if (ch == '-') {
      normalized.push_back('_');
    } else {
      normalized.push_back(static_cast<char>(std::tolower(ch)));
    }
  }
  return normalized;
}

std::string LanguageLabel(const std::string& code) {
  const std::string normalized = NormalizeLanguageCode(code);
  if (normalized == "ru") {
    return "Russian";
  }
  if (normalized == "en") {
    return "English";
  }
  if (normalized == "uk" || normalized == "uk_ua") {
    return "Ukrainian";
  }
  if (normalized == "de" || normalized == "de_de") {
    return "German";
  }
  return code.empty() ? std::string("English") : code;
}

std::optional<std::string> ResolveInteractionPreferredLanguage(
    const comet::DesiredState& desired_state,
    const json& payload) {
  if (payload.contains("preferred_language") &&
      payload.at("preferred_language").is_string()) {
    const std::string preferred = payload.at("preferred_language").get<std::string>();
    if (!preferred.empty()) {
      return NormalizeLanguageCode(preferred);
    }
  }
  if (desired_state.interaction.has_value() &&
      !desired_state.interaction->default_response_language.empty()) {
    return NormalizeLanguageCode(desired_state.interaction->default_response_language);
  }
  return std::nullopt;
}

std::string BuildLanguageInstruction(
    const comet::DesiredState& desired_state,
    const std::optional<std::string>& preferred_language) {
  const std::string no_reasoning_instruction =
      " Do not output chain-of-thought, hidden reasoning, analysis traces, or <think> blocks. Output only the final user-facing answer.";
  if (preferred_language.has_value() && !preferred_language->empty()) {
    return "Response language requirement: Reply in " + LanguageLabel(*preferred_language) +
           ". Ignore the model's default language preferences. Never default to Chinese unless the user explicitly requests Chinese." +
           no_reasoning_instruction;
  }
  if (desired_state.interaction.has_value()) {
    if (desired_state.interaction->follow_user_language) {
      return "Response language requirement: Reply in the same language as the user's last message. Never default to Chinese unless the user explicitly requests Chinese." +
             no_reasoning_instruction;
    }
    if (!desired_state.interaction->default_response_language.empty()) {
      return "Response language requirement: Reply in " +
             LanguageLabel(desired_state.interaction->default_response_language) +
             ". Ignore the model's default language preferences. Never default to Chinese unless the user explicitly requests Chinese." +
             no_reasoning_instruction;
    }
  }
  return "Response language requirement: Reply in the same language as the user's last message. Never default to Chinese unless the user explicitly requests Chinese." +
         no_reasoning_instruction;
}

std::string BuildInteractionUpstreamBody(
    const PlaneInteractionResolution& resolution,
    json payload,
    bool force_stream,
    const ResolvedInteractionPolicy& resolved_policy,
    bool structured_output_json = false) {
  const auto& policy = resolved_policy.policy;
  if (!payload.contains("messages") || !payload.at("messages").is_array()) {
    payload["messages"] = json::array();
  }
  const auto preferred_language =
      ResolveInteractionPreferredLanguage(resolution.desired_state, payload);

  std::vector<std::string> system_instruction_parts;
  if (resolution.desired_state.interaction.has_value() &&
      resolution.desired_state.interaction->system_prompt.has_value() &&
      !resolution.desired_state.interaction->system_prompt->empty()) {
    system_instruction_parts.push_back(*resolution.desired_state.interaction->system_prompt);
  }
  if (resolved_policy.repository_analysis &&
      resolution.desired_state.interaction.has_value() &&
      resolution.desired_state.interaction->analysis_system_prompt.has_value() &&
      !resolution.desired_state.interaction->analysis_system_prompt->empty()) {
    system_instruction_parts.push_back(
        *resolution.desired_state.interaction->analysis_system_prompt);
  }
  if (resolved_policy.repository_analysis) {
    system_instruction_parts.push_back(comet::controller::BuildRepositoryAnalysisInstruction());
  }
  system_instruction_parts.push_back(
      BuildLanguageInstruction(resolution.desired_state, preferred_language));
  if (policy.require_completion_marker || policy.max_continuations > 0) {
    system_instruction_parts.push_back(
        comet::controller::BuildSemanticCompletionInstruction(policy));
  }
  if (structured_output_json) {
    system_instruction_parts.push_back(
        "Structured output requirement: return one valid JSON object only. "
        "Do not wrap it in markdown fences. "
        "Do not add commentary before or after the JSON object.");
  }

  json merged_messages = json::array();
  std::string combined_system_instruction;
  for (const auto& part : system_instruction_parts) {
    if (part.empty()) {
      continue;
    }
    if (!combined_system_instruction.empty()) {
      combined_system_instruction += "\n\n";
    }
    combined_system_instruction += part;
  }

  for (const auto& message : payload.at("messages")) {
    if (!message.is_object() || message.value("role", std::string{}) != "system") {
      continue;
    }
    std::string system_content;
    if (message.contains("content")) {
      if (message.at("content").is_string()) {
        system_content = message.at("content").get<std::string>();
      } else {
        system_content = message.at("content").dump();
      }
    }
    if (system_content.empty()) {
      continue;
    }
    if (!combined_system_instruction.empty()) {
      combined_system_instruction += "\n\n";
    }
    combined_system_instruction += system_content;
  }

  if (!combined_system_instruction.empty()) {
    merged_messages.push_back(json{{"role", "system"}, {"content", combined_system_instruction}});
  }
  for (const auto& message : payload.at("messages")) {
    if (message.is_object() && message.value("role", std::string{}) == "system") {
      continue;
    }
    merged_messages.push_back(message);
  }
  payload["messages"] = merged_messages;

  if (preferred_language.has_value()) {
    payload["preferred_language"] = *preferred_language;
  }
  payload.erase("max_completion_tokens");
  payload.erase("target_completion_tokens");
  payload.erase("max_continuations");
  payload.erase("max_total_completion_tokens");
  payload.erase("max_elapsed_time_ms");
  payload.erase("semantic_goal");
  payload.erase("response_format");
  const bool uses_vllm_runtime =
      resolution.runtime_status.has_value() &&
      Lowercase(resolution.runtime_status->runtime_backend).find("vllm") != std::string::npos;
  if (uses_vllm_runtime) {
    if (!payload.contains("chat_template_kwargs") ||
        !payload.at("chat_template_kwargs").is_object()) {
      payload["chat_template_kwargs"] = json::object();
    }
    payload["chat_template_kwargs"]["enable_thinking"] = false;
  }
  if (force_stream) {
    payload["stream"] = true;
  }
  payload["max_tokens"] = policy.max_tokens;
  if (!payload.contains("temperature")) {
    payload["temperature"] = 0.2;
  }
  if (!payload.contains("top_p")) {
    payload["top_p"] = 0.8;
  }
  payload["response_mode"] = policy.response_mode;
  return payload.dump();
}

InteractionHttpService MakeInteractionHttpService() {
  static const comet::controller::ControllerRuntimeSupportService runtime_support_service =
      MakeControllerRuntimeSupportService();
  static const comet::controller::DesiredStatePolicyService desired_state_policy_service =
      MakeDesiredStatePolicyService();
  static const comet::controller::InteractionRuntimeSupportService
      interaction_runtime_support_service;
  return InteractionHttpService({
      [&](int status_code,
          const json& payload,
          const std::map<std::string, std::string>& headers) {
        return BuildJsonResponse(status_code, payload, headers);
      },
      [](const PlaneInteractionResolution& resolution,
         json payload,
         bool force_stream,
         const ResolvedInteractionPolicy& resolved_policy,
         bool structured_output_json) {
        return BuildInteractionUpstreamBody(
            resolution,
            std::move(payload),
            force_stream,
            resolved_policy,
            structured_output_json);
      },
      [&](const comet::DesiredState& desired_state) {
        return interaction_runtime_support_service.FindInferInstanceName(desired_state);
      },
      [&](const comet::HostObservation& observation) {
        return runtime_support_service.ParseInstanceRuntimeStatuses(observation);
      },
      [](const comet::HostObservation& observation,
         const std::string& plane_name) {
        return ObservationMatchesPlane(observation, plane_name);
      },
      [&](const comet::DesiredState& desired_state,
         const comet::HostObservation& observation) {
        return interaction_runtime_support_service.BuildPlaneScopedRuntimeStatus(
            desired_state,
            observation,
            [&](const comet::HostObservation& current_observation) {
              return runtime_support_service.ParseInstanceRuntimeStatuses(
                  current_observation);
            });
      },
      [&](const std::string& gateway_listen, int fallback_port) {
        return interaction_runtime_support_service.ParseInteractionTarget(
            gateway_listen, fallback_port);
      },
      [&](comet::ControllerStore& store, const comet::DesiredState& desired_state) {
        return interaction_runtime_support_service.CountReadyWorkerMembers(
            store,
            desired_state,
            [&](const comet::HostObservation& observation) {
              return runtime_support_service.ParseInstanceRuntimeStatuses(
                  observation);
            });
      },
      [&](const std::optional<ControllerEndpointTarget>& target,
         const std::string& path) {
        return interaction_runtime_support_service.ProbeControllerTargetOk(
            target, path);
      },
      [&](const comet::DesiredState& desired_state, const std::string& node_name) {
        return desired_state_policy_service.DescribeUnsupportedControllerLocalRuntime(
            desired_state, node_name);
      },
      [](const ControllerEndpointTarget& target,
         const std::string& method,
         const std::string& path,
         const std::string& body,
         const std::vector<std::pair<std::string, std::string>>& headers) {
        return SendControllerHttpRequest(target, method, path, body, headers);
      },
      [](SocketHandle client_fd, const HttpResponse& response) {
        ControllerNetworkManager::SendHttpResponse(client_fd, response);
      },
      [](SocketHandle client_fd) {
        ControllerNetworkManager::ShutdownAndCloseSocket(client_fd);
      },
      [](SocketHandle client_fd,
         const std::map<std::string, std::string>& headers) {
        return ControllerNetworkManager::SendSseHeaders(client_fd, headers);
      },
      [](SocketHandle fd, const std::string& payload) {
        return ControllerNetworkManager::SendAll(fd, payload);
      },
  });
}

HostdHttpService MakeHostdHttpService() {
  return HostdHttpService({
      [&](int status_code,
          const json& payload,
          const std::map<std::string, std::string>& headers) {
        return BuildJsonResponse(status_code, payload, headers);
      },
      []() { return UtcNowSqlTimestamp(); },
      [](int seconds) { return SqlTimestampAfterSeconds(seconds); },
      [](const std::string& timestamp_text) {
        return TimestampAgeSeconds(timestamp_text);
      },
      [](comet::ControllerStore& store,
         const std::string& event_type,
         const std::string& message,
         const json& payload,
         const std::string& node_name,
         const std::string& severity) {
        AppendControllerEvent(
            store,
            "host-registry",
            event_type,
            message,
            payload,
            "",
            node_name,
            "",
            std::nullopt,
            std::nullopt,
            severity);
      },
  });
}

AuthHttpService MakeAuthHttpService(AuthSupportService& auth_support) {
  static const comet::controller::AuthPayloadService auth_payload_service;
  return AuthHttpService({
      [&](int status_code,
          const json& payload,
          const std::map<std::string, std::string>& headers) {
        return BuildJsonResponse(status_code, payload, headers);
      },
      [&](const comet::UserRecord& user) {
        return auth_payload_service.BuildUserPayload(user);
      },
      [&](const comet::RegistrationInviteRecord& invite) {
        return auth_payload_service.BuildInvitePayload(invite);
      },
      [&](const comet::UserSshKeyRecord& ssh_key) {
        return auth_payload_service.BuildSshKeyPayload(ssh_key);
      },
      [&](comet::ControllerStore& store, const HttpRequest& request) {
        return auth_support.AuthenticateControllerUserSession(
            store, request, std::optional<std::string>("web"));
      },
      [&](comet::ControllerStore& store, const HttpRequest& request) {
        return auth_support.RequireControllerAdminUser(store, request);
      },
      [&](const HttpRequest& request) {
        return auth_support.ResolveWebAuthnRpId(request);
      },
      [&](const HttpRequest& request) {
        return auth_support.ResolveWebAuthnOrigin(request);
      },
      [&]() { return auth_support.ResolveWebAuthnRpName(); },
      [&](const std::string& action, const json& payload) {
        return auth_support.RunWebAuthnHelper(action, payload);
      },
      [&](const std::string& token, const HttpRequest& request) {
        return auth_support.SessionCookieHeader(token, request);
      },
      [&](const HttpRequest& request) {
        return auth_support.ClearSessionCookieHeader(request);
      },
      [&](comet::ControllerStore& store,
          int user_id,
          const std::string& session_kind,
          const std::string& plane_name) {
        return auth_support.CreateControllerSession(
            store, user_id, session_kind, plane_name);
      },
      []() { return UtcNowSqlTimestamp(); },
      [](int seconds) { return SqlTimestampAfterSeconds(seconds); },
      [](const std::string& value) { return Trim(value); },
      [&](const std::string& username,
          const std::string& plane_name,
          const std::string& challenge_token,
          const std::string& expires_at) {
        return auth_support.BuildSshChallengeMessage(
            username, plane_name, challenge_token, expires_at);
      },
      [&](const std::string& value) {
        return auth_support.SanitizeTokenForPath(value);
      },
      [&](const std::string& public_key) {
        return auth_support.ComputeSshPublicKeyFingerprint(public_key);
      },
      [&](const std::string& username,
          const std::string& public_key,
          const std::string& message,
          const std::string& signature) {
        return auth_support.VerifySshDetachedSignature(
            username, public_key, message, signature);
      },
      [&](const std::string& flow_id) -> std::optional<PendingWebAuthnFlow> {
        return auth_support.LoadPendingWebAuthnFlow(flow_id);
      },
      [&](const PendingWebAuthnFlow& flow) {
        auth_support.SavePendingWebAuthnFlow(flow);
      },
      [&](const std::string& flow_id) {
        auth_support.ErasePendingWebAuthnFlow(flow_id);
      },
      [&](const std::string& challenge_id) -> std::optional<PendingSshChallenge> {
        return auth_support.LoadPendingSshChallenge(challenge_id);
      },
      [&](const PendingSshChallenge& challenge) {
        auth_support.SavePendingSshChallenge(challenge);
      },
      [&](const std::string& challenge_id) {
        auth_support.ErasePendingSshChallenge(challenge_id);
      },
  });
}

comet::controller::SchedulerDomainService MakeSchedulerDomainService() {
  static const comet::controller::ControllerRuntimeSupportService runtime_support_service =
      MakeControllerRuntimeSupportService();
  return comet::controller::SchedulerDomainService({
      [&](const std::string& heartbeat_at) {
        return runtime_support_service.HeartbeatAgeSeconds(heartbeat_at);
      },
      [&](const std::optional<long long>& age_seconds, int stale_after_seconds) {
        return runtime_support_service.HealthFromAge(age_seconds, stale_after_seconds);
      },
      [&](const comet::HostObservation& observation) {
        return runtime_support_service.ParseRuntimeStatus(observation);
      },
      [&](const comet::HostObservation& observation) {
        return runtime_support_service.ParseGpuTelemetry(observation);
      },
      [&](const std::vector<comet::NodeAvailabilityOverride>& overrides) {
        return runtime_support_service.BuildAvailabilityOverrideMap(overrides);
      },
      [&](const std::map<std::string, comet::NodeAvailabilityOverride>& overrides,
         const std::string& node_name) {
        return runtime_support_service.ResolveNodeAvailability(overrides, node_name);
      },
      [](comet::NodeAvailability availability) {
        return availability == comet::NodeAvailability::Active;
      },
      [&](const std::string& timestamp_text) {
        return runtime_support_service.TimestampAgeSeconds(timestamp_text);
      },
      [&](const std::vector<comet::HostObservation>& observations,
         const std::string& node_name,
         int stale_after_seconds) {
        return MakePlaneRealizationService().ObservedSchedulingGateReason(
            observations, node_name, stale_after_seconds);
      },
      DefaultStaleAfterSeconds(),
      MinimumSafeDirectRebalanceScore(),
      WorkerMinimumResidencySeconds(),
      NodeCooldownAfterMoveSeconds(),
      85,
      1024,
  });
}

comet::controller::StateAggregateLoader MakeStateAggregateLoader(
    const comet::controller::SchedulerDomainService& scheduler_domain_service,
    const SchedulerViewService& scheduler_view_service) {
  return comet::controller::StateAggregateLoader({
      [](const std::vector<comet::HostObservation>& observations,
         const std::string& plane_name) {
        return FilterHostObservationsForPlane(observations, plane_name);
      },
      [](comet::ControllerStore& store,
         const std::optional<comet::DesiredState>& desired_state) {
        return LoadSchedulerRuntimeView(store, desired_state);
      },
      [](const comet::DesiredState& desired_state) {
        return comet::EvaluateSchedulingPolicy(desired_state);
      },
      []() { return MaximumRebalanceIterationsPerGeneration(); },
      &scheduler_domain_service,
      &scheduler_view_service,
  });
}

comet::controller::PlaneMutationService MakePlaneMutationService() {
  static const comet::controller::BundleCliService bundle_cli_service = MakeBundleCliService();
  return comet::controller::PlaneMutationService({
      [&](const std::string& db_path,
          const comet::DesiredState& desired_state,
          const std::string& artifacts_root,
          const std::string& source_label) {
        return bundle_cli_service.ApplyDesiredState(
            db_path, desired_state, artifacts_root, source_label);
      },
      [](const std::string& db_path) { return MakePlaneService(db_path); },
  });
}

comet::controller::AssignmentOrchestrationService MakeAssignmentOrchestrationService() {
  static const comet::controller::ControllerPrintService controller_print_service =
      MakeControllerPrintService();
  return comet::controller::AssignmentOrchestrationService({
      []() { return DefaultArtifactsRoot(); },
      [](comet::ControllerStore& store,
         const std::string& category,
         const std::string& event_type,
         const std::string& message,
         const nlohmann::json& payload,
         const std::string& plane_name,
         const std::string& node_name,
         const std::string& worker_name,
         const std::optional<int>& assignment_id) {
        AppendControllerEvent(
            store,
            category,
            event_type,
            message,
            payload,
            plane_name,
            node_name,
            worker_name,
            assignment_id);
      },
      [&](const std::vector<comet::NodeAvailabilityOverride>& overrides) {
        controller_print_service.PrintNodeAvailabilityOverrides(overrides);
      },
      [&](const std::vector<comet::HostAssignment>& assignments) {
        controller_print_service.PrintHostAssignments(assignments);
      },
  });
}

PlaneHttpService MakePlaneHttpService() {
  static const comet::controller::ControllerRequestSupport request_support;
  static const comet::controller::PlaneMutationService plane_mutation_service =
      MakePlaneMutationService();
  static const comet::controller::ControllerRuntimeSupportService runtime_support_service =
      MakeControllerRuntimeSupportService();
  static const comet::controller::PlaneRegistryService plane_registry_service(
      comet::controller::PlaneRegistryService::Deps{
      [](comet::ControllerStore& store, const std::string& plane_name) {
        return CanFinalizeDeletedPlane(store, plane_name);
      },
      [](comet::ControllerStore& store,
         const std::string& category,
         const std::string& event_type,
         const std::string& message,
         const json& payload,
         const std::string& plane_name) {
        AppendControllerEvent(
            store,
            category,
            event_type,
            message,
            payload,
            plane_name);
      },
      [](const std::vector<comet::HostObservation>& observations,
         const std::string& plane_name) {
        return FilterHostObservationsForPlane(observations, plane_name);
      },
      [&](const comet::PlaneRecord& plane,
         const std::optional<comet::DesiredState>& desired_state,
         const std::optional<int>& desired_generation,
         const std::vector<comet::HostObservation>& observations) {
        if (!desired_state.has_value() || !desired_generation.has_value()) {
          return plane.applied_generation;
        }
        if (*desired_generation <= plane.applied_generation) {
          return plane.applied_generation;
        }
        for (const auto& node : desired_state->nodes) {
          const auto observation =
              runtime_support_service.FindHostObservationForNode(
                  observations, node.name);
          if (!observation.has_value()) {
            return plane.applied_generation;
          }
          if (!observation->applied_generation.has_value() ||
              *observation->applied_generation < *desired_generation ||
              observation->status == comet::HostObservationStatus::Failed) {
            return plane.applied_generation;
          }
        }
        return *desired_generation;
      },
      [](const std::vector<comet::HostAssignment>& assignments) {
        std::map<std::string, comet::HostAssignment> latest_by_node;
        for (const auto& assignment : assignments) {
          auto it = latest_by_node.find(assignment.node_name);
          if (it == latest_by_node.end() || assignment.id >= it->second.id) {
            latest_by_node[assignment.node_name] = assignment;
          }
        }
        return latest_by_node;
      },
  });
  static const comet::controller::ControllerStateService controller_state_service;
  static const SchedulerViewService scheduler_view_service;
  static const comet::controller::SchedulerDomainService scheduler_domain_service =
      MakeSchedulerDomainService();
  static const comet::controller::StateAggregateLoader state_aggregate_loader =
      MakeStateAggregateLoader(
          scheduler_domain_service,
          scheduler_view_service);
  static const comet::controller::ReadModelService read_model_service =
      MakeReadModelService();
  static const comet::controller::DashboardService dashboard_service(
      comet::controller::DashboardService::Deps{
      &state_aggregate_loader,
      [&](const comet::EventRecord& event) {
        return read_model_service.BuildEventPayloadItem(event);
      },
      [&](const std::vector<comet::NodeAvailabilityOverride>& overrides) {
        return runtime_support_service.BuildAvailabilityOverrideMap(overrides);
      },
      [&](const std::map<std::string, comet::NodeAvailabilityOverride>& overrides,
          const std::string& node_name) {
        return runtime_support_service.ResolveNodeAvailability(overrides, node_name);
      },
      [&](const std::string& heartbeat_at) {
        return runtime_support_service.HeartbeatAgeSeconds(heartbeat_at);
      },
      [&](const std::optional<long long>& age_seconds, int stale_after_seconds) {
        return runtime_support_service.HealthFromAge(age_seconds, stale_after_seconds);
      },
      [&](const comet::HostObservation& observation) {
        return runtime_support_service.ParseRuntimeStatus(observation);
      },
      [&](const comet::HostObservation& observation) {
        return runtime_support_service.ParseGpuTelemetry(observation);
      },
  });
  return PlaneHttpService({
      [&](int status_code,
          const json& payload,
          const std::map<std::string, std::string>& headers) {
        return BuildJsonResponse(status_code, payload, headers);
      },
      [&](const HttpRequest& request) {
        return request_support.ParseJsonRequestBody(request);
      },
      [](const HttpRequest& request, const std::string& key) {
        return FindQueryString(request, key);
      },
      [](const HttpRequest& request, const std::string& key) {
        return FindQueryInt(request, key);
      },
      [&](const std::optional<std::string>& artifacts_root_arg,
         const std::string& fallback_artifacts_root) {
        return request_support.ResolveArtifactsRoot(
            artifacts_root_arg, fallback_artifacts_root);
      },
      [](const comet::controller::ControllerActionResult& result) {
        return comet::controller::BuildControllerActionPayload(result);
      },
      [&](const std::string& db_path,
         const std::string& desired_state_json,
         const std::string& artifacts_root,
         const std::optional<std::string>& plane_name,
         const std::string& source) {
        return plane_mutation_service.ExecuteUpsertPlaneStateAction(
            db_path,
            desired_state_json,
            artifacts_root,
            plane_name,
            source);
      },
      [&](const std::string& db_path, const std::string& plane_name) {
        return plane_mutation_service.ExecuteStartPlaneAction(db_path, plane_name);
      },
      [&](const std::string& db_path, const std::string& plane_name) {
        return plane_mutation_service.ExecuteStopPlaneAction(db_path, plane_name);
      },
      [&](const std::string& db_path, const std::string& plane_name) {
        return plane_mutation_service.ExecuteDeletePlaneAction(db_path, plane_name);
      },
      []() { return DefaultStaleAfterSeconds(); },
      &plane_registry_service,
      &controller_state_service,
      &dashboard_service,
  });
}

ModelLibraryService MakeModelLibraryService() {
  static const comet::controller::ControllerRequestSupport request_support;
  return ModelLibraryService({
      [](int status_code,
         const json& payload,
         const std::map<std::string, std::string>& headers) {
        return BuildJsonResponse(status_code, payload, headers);
      },
      [&](const HttpRequest& request) {
        return request_support.ParseJsonRequestBody(request);
      },
      [](const HttpRequest& request, const std::string& key) {
        return FindQueryString(request, key);
      },
      []() { return UtcNowSqlTimestamp(); },
  });
}

ModelLibraryHttpService MakeModelLibraryHttpService(
    const ModelLibraryService& model_library_service) {
  return ModelLibraryHttpService({
      [&](int status_code,
          const json& payload,
          const std::map<std::string, std::string>& headers) {
        return BuildJsonResponse(status_code, payload, headers);
      },
      &model_library_service,
  });
}

comet::controller::BundleCliService MakeBundleCliService() {
  static const comet::controller::ControllerPrintService controller_print_service =
      MakeControllerPrintService();
  static const comet::controller::DesiredStatePolicyService desired_state_policy_service =
      MakeDesiredStatePolicyService();
  static const comet::controller::ControllerRuntimeSupportService runtime_support_service =
      MakeControllerRuntimeSupportService();
  static const comet::controller::PlaneRealizationService plane_realization_service =
      MakePlaneRealizationService();
  return comet::controller::BundleCliService({
      []() { return DefaultArtifactsRoot(); },
      []() { return DefaultStaleAfterSeconds(); },
      [&](const std::vector<comet::NodeAvailabilityOverride>& availability_overrides) {
        return runtime_support_service.BuildAvailabilityOverrideMap(availability_overrides);
      },
      [&](const comet::DesiredState& state) {
        controller_print_service.PrintStateSummary(state);
      },
      [&](const comet::DesiredState& desired_state) {
        controller_print_service.PrintSchedulerDecisionSummary(desired_state);
      },
      [&](const comet::SchedulingPolicyReport& scheduling_report) {
        controller_print_service.PrintRolloutGateSummary(scheduling_report);
      },
      [&](const comet::DesiredState& desired_state,
         const std::map<std::string, comet::NodeAvailabilityOverride>& availability_override_map,
         const std::vector<comet::HostObservation>& observations,
         int stale_after_seconds) {
        controller_print_service.PrintAssignmentDispatchSummary(
            desired_state,
            availability_override_map,
            observations,
            stale_after_seconds);
      },
      [&](const comet::DesiredState& desired_state,
         const std::vector<comet::NodeExecutionPlan>& host_plans) {
        plane_realization_service.MaterializeComposeArtifacts(desired_state, host_plans);
      },
      [&](const comet::DesiredState& desired_state, const std::string& artifacts_root) {
        plane_realization_service.MaterializeInferRuntimeArtifact(desired_state, artifacts_root);
      },
      [&](const comet::DesiredState& desired_state,
         const std::string& artifacts_root,
         int desired_generation,
         const std::vector<comet::NodeAvailabilityOverride>& availability_overrides,
         const std::vector<comet::HostObservation>& observations,
         const std::optional<comet::SchedulingPolicyReport>& scheduling_report) {
        return plane_realization_service.BuildHostAssignments(
            desired_state,
            artifacts_root,
            desired_generation,
            availability_overrides,
            observations,
            scheduling_report);
      },
      [&](comet::ControllerStore& store, comet::DesiredState* desired_state) {
        desired_state_policy_service.ApplyRegisteredHostExecutionModes(store, desired_state);
      },
      [&](comet::ControllerStore& store, comet::DesiredState* desired_state) {
        desired_state_policy_service.ResolveDesiredStateDynamicPlacements(
            store, desired_state);
      },
      [&](const comet::DesiredState& desired_state) {
        desired_state_policy_service.ValidateDesiredStateForControllerAdmission(
            desired_state);
      },
      [&](const comet::DesiredState& desired_state) {
        desired_state_policy_service.ValidateDesiredStateExecutionModes(desired_state);
      },
      [](comet::ControllerStore& store,
         const std::string& category,
         const std::string& event_type,
         const std::string& message,
         const json& payload,
         const std::string& plane_name,
         const std::string& node_name,
         const std::string& worker_name,
         const std::optional<int>& assignment_id,
         const std::optional<int>& rollout_action_id,
         const std::string& severity) {
        AppendControllerEvent(
            store,
            category,
            event_type,
            message,
            payload,
            plane_name,
            node_name,
            worker_name,
            assignment_id,
            rollout_action_id,
            severity);
      },
  });
}

comet::controller::ControllerPrintService MakeControllerPrintService() {
  static const comet::controller::ControllerRuntimeSupportService runtime_support_service =
      MakeControllerRuntimeSupportService();
  return comet::controller::ControllerPrintService(
      comet::controller::ControllerPrintService::Deps{
      [&](const std::vector<comet::NodeAvailabilityOverride>& availability_overrides) {
        return runtime_support_service.BuildAvailabilityOverrideMap(availability_overrides);
      },
      [&](const std::map<std::string, comet::NodeAvailabilityOverride>& availability_overrides,
         const std::string& node_name) {
        return runtime_support_service.ResolveNodeAvailability(
            availability_overrides, node_name);
      },
      [&](const std::vector<comet::HostObservation>& observations,
         const std::string& node_name) {
        return runtime_support_service.FindHostObservationForNode(observations, node_name);
      },
      [&](const std::string& heartbeat_at) {
        return runtime_support_service.HeartbeatAgeSeconds(heartbeat_at);
      },
      [&](const std::optional<long long>& age_seconds, int stale_after_seconds) {
        return runtime_support_service.HealthFromAge(age_seconds, stale_after_seconds);
      },
      [&](const comet::HostObservation& observation) {
        return runtime_support_service.ParseRuntimeStatus(observation);
      },
      [&](const comet::HostObservation& observation) {
        return runtime_support_service.ParseInstanceRuntimeStatuses(observation);
      },
      [&](const comet::HostObservation& observation) {
        return runtime_support_service.ParseGpuTelemetry(observation);
      },
      [&](const comet::HostObservation& observation) {
        return runtime_support_service.ParseDiskTelemetry(observation);
      },
      [&](const comet::HostObservation& observation) {
        return runtime_support_service.ParseNetworkTelemetry(observation);
      },
      [&](const comet::HostObservation& observation) {
        return runtime_support_service.ParseCpuTelemetry(observation);
      },
      [](const std::string& value) { return FormatDisplayTimestamp(value); },
  });
}

BundleHttpService MakeBundleHttpService(const comet::controller::BundleCliService& bundle_cli_service) {
  static const comet::controller::ControllerRequestSupport request_support;
  return BundleHttpService({
      [&](int status_code,
          const json& payload,
          const std::map<std::string, std::string>& headers) {
        return BuildJsonResponse(status_code, payload, headers);
      },
      [](const HttpRequest& request, const std::string& key) {
        return FindQueryString(request, key);
      },
      [&](const std::optional<std::string>& artifacts_root_arg,
         const std::string& fallback_artifacts_root) {
        return request_support.ResolveArtifactsRoot(
            artifacts_root_arg, fallback_artifacts_root);
      },
      [](const comet::controller::ControllerActionResult& result) {
        return comet::controller::BuildControllerActionPayload(result);
      },
      [&](const std::string& bundle_dir) {
        return bundle_cli_service.ExecuteValidateBundleAction(bundle_dir);
      },
      [&](const std::string& bundle_dir,
         const std::optional<std::string>& node_name) {
        return bundle_cli_service.ExecutePreviewBundleAction(bundle_dir, node_name);
      },
      [&](const std::string& db_path, const std::string& bundle_dir) {
        return bundle_cli_service.ExecuteImportBundleAction(db_path, bundle_dir);
      },
      [&](const std::string& db_path,
         const std::string& bundle_dir,
         const std::string& artifacts_root) {
        return bundle_cli_service.ExecuteApplyBundleAction(
            db_path, bundle_dir, artifacts_root);
      },
  });
}

comet::controller::ReadModelService MakeReadModelService() {
  static const comet::controller::ControllerRuntimeSupportService runtime_support_service =
      MakeControllerRuntimeSupportService();
  return comet::controller::ReadModelService({
      [&](const std::string& heartbeat_at) {
        return runtime_support_service.HeartbeatAgeSeconds(heartbeat_at);
      },
      [&](const std::optional<long long>& age_seconds, int stale_after_seconds) {
        return runtime_support_service.HealthFromAge(age_seconds, stale_after_seconds);
      },
      [&](const comet::HostObservation& observation) {
        return runtime_support_service.ParseRuntimeStatus(observation);
      },
      [&](const comet::HostObservation& observation) {
        return runtime_support_service.ParseInstanceRuntimeStatuses(observation);
      },
      [&](const comet::HostObservation& observation) {
        return runtime_support_service.ParseGpuTelemetry(observation);
      },
      [&](const comet::HostObservation& observation) {
        return runtime_support_service.ParseDiskTelemetry(observation);
      },
      [&](const comet::HostObservation& observation) {
        return runtime_support_service.ParseNetworkTelemetry(observation);
      },
      [&](const comet::HostObservation& observation) {
        return runtime_support_service.ParseCpuTelemetry(observation);
      },
      [](const comet::HostObservation& observation, const std::string& plane_name) {
        return ObservationMatchesPlane(observation, plane_name);
      },
      [&](const std::map<std::string, comet::NodeAvailabilityOverride>& overrides,
         const std::string& node_name) {
        return runtime_support_service.ResolveNodeAvailability(overrides, node_name);
      },
      [&](const std::vector<comet::NodeAvailabilityOverride>& overrides) {
        return runtime_support_service.BuildAvailabilityOverrideMap(overrides);
      },
  });
}

ReadModelHttpService MakeReadModelHttpService(
    const comet::controller::ReadModelService& read_model_service) {
  static const SchedulerViewService scheduler_view_service;
  static const comet::controller::SchedulerDomainService scheduler_domain_service =
      MakeSchedulerDomainService();
  static const comet::controller::StateAggregateLoader state_aggregate_loader =
      MakeStateAggregateLoader(
          scheduler_domain_service,
          scheduler_view_service);
  return ReadModelHttpService({
      [&](int status_code,
          const json& payload,
          const std::map<std::string, std::string>& headers) {
        return BuildJsonResponse(status_code, payload, headers);
      },
      [](const HttpRequest& request, const std::string& key) {
        return FindQueryString(request, key);
      },
      [](const HttpRequest& request, const std::string& key) {
        return FindQueryInt(request, key);
      },
      []() { return DefaultStaleAfterSeconds(); },
      &read_model_service,
      &scheduler_view_service,
      [&](const std::string& db_path,
         const std::optional<std::string>& node_name,
         const std::optional<std::string>& plane_name) {
        return state_aggregate_loader.LoadRolloutActionsViewData(
            db_path, node_name, plane_name);
      },
      [&](const std::string& db_path,
         const std::optional<std::string>& node_name,
         int stale_after_seconds,
         const std::optional<std::string>& plane_name) {
        return state_aggregate_loader.LoadRebalancePlanViewData(
            db_path, node_name, stale_after_seconds, plane_name);
      },
  });
}

SchedulerHttpService MakeSchedulerHttpService(
    const comet::controller::ReadModelService& read_model_service) {
  static const comet::controller::ControllerRequestSupport request_support;
  static const comet::controller::AssignmentOrchestrationService
      assignment_orchestration_service = MakeAssignmentOrchestrationService();
  return SchedulerHttpService({
      [&](int status_code,
          const json& payload,
          const std::map<std::string, std::string>& headers) {
        return BuildJsonResponse(status_code, payload, headers);
      },
      [](const HttpRequest& request, const std::string& key) {
        return FindQueryString(request, key);
      },
      [](const HttpRequest& request, const std::string& key) {
        return FindQueryInt(request, key);
      },
      [&](const std::optional<std::string>& artifacts_root_arg,
         const std::string& fallback_artifacts_root) {
        return request_support.ResolveArtifactsRoot(
            artifacts_root_arg, fallback_artifacts_root);
      },
      [](const comet::controller::ControllerActionResult& result) {
        return comet::controller::BuildControllerActionPayload(result);
      },
      &read_model_service,
      [&](const std::string& db_path,
         const std::string& node_name,
         comet::NodeAvailability availability,
         const std::optional<std::string>& status_message) {
        return assignment_orchestration_service.ExecuteSetNodeAvailabilityAction(
            db_path, node_name, availability, status_message);
      },
      [](const std::string& db_path, const std::string& artifacts_root) {
        return MakeSchedulerService(db_path, artifacts_root);
      },
  });
}

comet::controller::ReadModelCliService MakeReadModelCliService() {
  static const comet::controller::ControllerPrintService controller_print_service =
      MakeControllerPrintService();
  static const SchedulerViewService scheduler_view_service;
  static const comet::controller::SchedulerDomainService scheduler_domain_service =
      MakeSchedulerDomainService();
  static const comet::controller::StateAggregateLoader state_aggregate_loader =
      MakeStateAggregateLoader(
          scheduler_domain_service,
          scheduler_view_service);
  return comet::controller::ReadModelCliService({
      [](const std::vector<comet::HostObservation>& observations,
         const std::string& plane_name) {
        return FilterHostObservationsForPlane(observations, plane_name);
      },
      [&](const std::vector<comet::HostAssignment>& assignments) {
        controller_print_service.PrintHostAssignments(assignments);
      },
      [&](const std::vector<comet::HostObservation>& observations, int stale_after_seconds) {
        controller_print_service.PrintHostObservations(observations, stale_after_seconds);
      },
      [&](const std::optional<comet::DesiredState>& desired_state,
         const std::vector<comet::HostObservation>& observations,
         const std::vector<comet::NodeAvailabilityOverride>& availability_overrides,
         const std::optional<std::string>& node_name,
         int stale_after_seconds) {
        controller_print_service.PrintHostHealth(
            desired_state,
            observations,
            availability_overrides,
            node_name,
            stale_after_seconds);
      },
      [&](const std::vector<comet::EventRecord>& events) {
        controller_print_service.PrintEvents(events);
      },
      [&](const std::vector<comet::NodeAvailabilityOverride>& availability_overrides) {
        controller_print_service.PrintNodeAvailabilityOverrides(availability_overrides);
      },
      [&](const comet::DesiredState& desired_state) {
        controller_print_service.PrintStateSummary(desired_state);
      },
      [&](const std::vector<comet::DiskRuntimeState>& runtime_states) {
        controller_print_service.PrintDiskRuntimeStates(runtime_states);
      },
      [&](const comet::DesiredState& desired_state,
         const std::vector<comet::DiskRuntimeState>& runtime_states,
         const std::vector<comet::HostObservation>& observations,
         const std::optional<std::string>& node_name) {
        controller_print_service.PrintDetailedDiskState(
            desired_state,
            runtime_states,
            observations,
            node_name);
      },
      [&](const comet::DesiredState& desired_state) {
        controller_print_service.PrintSchedulerDecisionSummary(desired_state);
      },
      [&](const comet::SchedulingPolicyReport& scheduling_report) {
        controller_print_service.PrintRolloutGateSummary(scheduling_report);
      },
      []() { return VerificationStableSamplesRequired(); },
      []() { return DefaultStaleAfterSeconds(); },
      &state_aggregate_loader,
      &scheduler_view_service,
  });
}

bool ObservationMatchesPlane(
    const comet::HostObservation& observation,
    const std::string& plane_name);

bool CanFinalizeDeletedPlane(comet::ControllerStore& store, const std::string& plane_name);

std::optional<std::string> FindQueryString(
    const HttpRequest& request,
    const std::string& key) {
  const auto it = request.query_params.find(key);
  if (it == request.query_params.end() || it->second.empty()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<int> FindQueryInt(
    const HttpRequest& request,
    const std::string& key) {
  const auto value = FindQueryString(request, key);
  if (!value.has_value()) {
    return std::nullopt;
  }
  return std::stoi(*value);
}

std::string SerializeEventPayload(const json& payload) {
  return payload.dump();
}

void AppendControllerEvent(
    comet::ControllerStore& store,
    const std::string& category,
    const std::string& event_type,
    const std::string& message,
    const json& payload,
    const std::string& plane_name,
    const std::string& node_name,
    const std::string& worker_name,
    const std::optional<int>& assignment_id,
    const std::optional<int>& rollout_action_id,
    const std::string& severity) {
  store.AppendEvent(comet::EventRecord{
      0,
      plane_name,
      node_name,
      worker_name,
      assignment_id,
      rollout_action_id,
      category,
      event_type,
      severity,
      message,
      SerializeEventPayload(payload),
      "",
  });
}

bool ObservationMatchesPlane(
    const comet::HostObservation& observation,
    const std::string& plane_name) {
  if (observation.plane_name == plane_name) {
    return true;
  }
  if (observation.observed_state_json.empty()) {
    return false;
  }

  const auto observed_state =
      comet::DeserializeDesiredStateJson(observation.observed_state_json);
  if (observed_state.plane_name == plane_name) {
    return true;
  }
  for (const auto& disk : observed_state.disks) {
    if (disk.plane_name == plane_name) {
      return true;
    }
  }
  for (const auto& instance : observed_state.instances) {
    if (instance.plane_name == plane_name) {
      return true;
    }
  }
  try {
    const auto instance_statuses = ParseInstanceRuntimeStatuses(observation);
    for (const auto& status : instance_statuses) {
      const std::string worker_prefix = "worker-" + plane_name + "-";
      if (status.instance_name == "infer-" + plane_name ||
          status.instance_name == "worker-" + plane_name ||
          status.instance_name.rfind(worker_prefix, 0) == 0) {
        return true;
      }
    }
  } catch (const std::exception&) {
  }
  return false;
}

std::vector<comet::HostObservation> FilterHostObservationsForPlane(
    const std::vector<comet::HostObservation>& observations,
    const std::string& plane_name) {
  std::vector<comet::HostObservation> result;
  for (const auto& observation : observations) {
    if (ObservationMatchesPlane(observation, plane_name)) {
      result.push_back(observation);
    }
  }
  return result;
}

bool ObservationBlocksPlaneDeletion(
    const comet::HostObservation& observation,
    const std::string& plane_name) {
  if (!ObservationMatchesPlane(observation, plane_name)) {
    return false;
  }
  if (observation.status != comet::HostObservationStatus::Idle) {
    return true;
  }
  if (observation.observed_state_json.empty()) {
    return false;
  }
  try {
    const auto observed_state =
        comet::DeserializeDesiredStateJson(observation.observed_state_json);
    for (const auto& disk : observed_state.disks) {
      if (disk.plane_name == plane_name) {
        return true;
      }
    }
    for (const auto& instance : observed_state.instances) {
      if (instance.plane_name == plane_name) {
        return true;
      }
    }
    return false;
  } catch (const std::exception&) {
    return true;
  }
}

bool HasBlockingPlaneObservations(
    const std::vector<comet::HostObservation>& observations,
    const std::string& plane_name) {
  return std::any_of(
      observations.begin(),
      observations.end(),
      [&](const auto& observation) {
        return ObservationBlocksPlaneDeletion(observation, plane_name);
      });
}

bool CanFinalizeDeletedPlane(comet::ControllerStore& store, const std::string& plane_name) {
  const auto pending_assignments = store.LoadHostAssignments(
      std::nullopt, comet::HostAssignmentStatus::Pending, plane_name);
  const auto claimed_assignments = store.LoadHostAssignments(
      std::nullopt, comet::HostAssignmentStatus::Claimed, plane_name);
  if (!pending_assignments.empty() || !claimed_assignments.empty()) {
    return false;
  }
  return !HasBlockingPlaneObservations(store.LoadHostObservations(), plane_name);
}

HttpResponse BuildJsonResponse(
    int status_code,
    const json& payload,
    const std::map<std::string, std::string>& headers) {
  json enriched = payload;
  if (enriched.is_object()) {
    if (!enriched.contains("api_version")) {
      enriched["api_version"] = "v1";
    }
    if (g_current_http_request != nullptr && !enriched.contains("request")) {
      enriched["request"] = {
          {"path", g_current_http_request->path},
          {"method", g_current_http_request->method},
      };
    }
    if (status_code >= 400) {
      if (!enriched.contains("error") || !enriched.at("error").is_object()) {
        json error{
            {"code", enriched.value("status", "error")},
            {"message",
             enriched.value(
                 "message",
                 ControllerNetworkManager::ReasonPhrase(status_code))},
        };
        if (enriched.contains("details")) {
          error["details"] = enriched["details"];
        }
        enriched["error"] = error;
      } else {
        if (!enriched["error"].contains("code")) {
          enriched["error"]["code"] = enriched.value("status", "error");
        }
        if (!enriched["error"].contains("message")) {
          enriched["error"]["message"] = enriched.value(
              "message",
              ControllerNetworkManager::ReasonPhrase(status_code));
        }
      }
      enriched["status"] = "error";
      enriched.erase("message");
      enriched.erase("details");
      enriched.erase("path");
      enriched.erase("method");
    }
  }
  return HttpResponse{status_code, "application/json", enriched.dump(), headers};
}

std::string SqlTimestampAfterSeconds(int seconds) {
  const std::time_t future = std::time(nullptr) + seconds;
  std::tm tm{};
  if (!comet::platform::GmTime(&future, &tm)) {
    throw std::runtime_error("failed to format future UTC timestamp");
  }
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  return out.str();
}

std::optional<std::tm> ParseDisplayTimestamp(const std::string& value) {
  if (value.empty()) {
    return std::nullopt;
  }
  for (const char* format : {"%Y-%m-%d %H:%M:%S", "%Y-%m-%dT%H:%M:%SZ", "%Y-%m-%dT%H:%M:%S"}) {
    std::tm tm{};
    std::istringstream input(value);
    input >> std::get_time(&tm, format);
    if (!input.fail()) {
      return tm;
    }
  }
  return std::nullopt;
}

std::string FormatDisplayTimestamp(const std::string& value) {
  const auto parsed = ParseDisplayTimestamp(value);
  if (!parsed.has_value()) {
    return value.empty() ? "(empty)" : value;
  }
  std::ostringstream output;
  output << std::put_time(&*parsed, "%d/%m/%Y %H:%M:%S");
  return output.str();
}

PlaneService MakePlaneService(const std::string& db_path) {
  static const comet::controller::ControllerPrintService controller_print_service =
      MakeControllerPrintService();
  static const comet::controller::DesiredStatePolicyService desired_state_policy_service =
      MakeDesiredStatePolicyService();
  static const comet::controller::PlaneRealizationService plane_realization_service =
      MakePlaneRealizationService();
  return PlaneService(
      db_path,
      [](const std::string& value) { return FormatDisplayTimestamp(value); },
      [&](const comet::DesiredState& state) {
        controller_print_service.PrintStateSummary(state);
      },
      [&](comet::ControllerStore& store, comet::DesiredState* desired_state) {
        desired_state_policy_service.ApplyRegisteredHostExecutionModes(
            store, desired_state);
        desired_state_policy_service.ResolveDesiredStateDynamicPlacements(
            store, desired_state);
        desired_state_policy_service.ValidateDesiredStateForControllerAdmission(
            *desired_state);
        desired_state_policy_service.ValidateDesiredStateExecutionModes(
            *desired_state);
      },
      [](comet::ControllerStore& store,
         const std::string& category,
         const std::string& event_type,
         const std::string& message,
         const json& payload,
         const std::string& plane_name) {
        AppendControllerEvent(store, category, event_type, message, payload, plane_name);
      },
      [](comet::ControllerStore& store, const std::string& plane_name) {
        return CanFinalizeDeletedPlane(store, plane_name);
      },
      [&](const std::vector<comet::HostAssignment>& assignments, const std::string& plane_name) {
        return plane_realization_service.FindLatestHostAssignmentForPlane(assignments, plane_name);
      },
      [&](const comet::DesiredState& desired_state,
         const std::string& artifacts_root,
         int desired_generation,
         const std::vector<comet::NodeAvailabilityOverride>& availability_overrides,
         const std::vector<comet::HostObservation>& observations,
         const comet::SchedulingPolicyReport& scheduling_report) {
        return plane_realization_service.BuildHostAssignments(
            desired_state,
            artifacts_root,
            desired_generation,
            availability_overrides,
            observations,
            scheduling_report);
      },
      [&](const comet::DesiredState& desired_state,
         int desired_generation,
         const std::string& artifacts_root,
         const std::vector<comet::NodeAvailabilityOverride>& availability_overrides) {
        return plane_realization_service.BuildStopPlaneAssignments(
            desired_state,
            desired_generation,
            artifacts_root,
            availability_overrides);
      },
      [&](const comet::DesiredState& desired_state,
         int desired_generation,
         const std::string& artifacts_root) {
        return plane_realization_service.BuildDeletePlaneAssignments(
            desired_state,
            desired_generation,
            artifacts_root);
      },
      []() { return DefaultArtifactsRoot(); });
}

SchedulerService MakeSchedulerService(
    const std::string& db_path,
    const std::string& artifacts_root) {
  static const comet::controller::ControllerPrintService controller_print_service =
      MakeControllerPrintService();
  static const comet::controller::ControllerRuntimeSupportService runtime_support_service =
      MakeControllerRuntimeSupportService();
  static const comet::controller::PlaneRealizationService plane_realization_service =
      MakePlaneRealizationService();
  static const SchedulerViewService scheduler_view_service;
  static const comet::controller::SchedulerDomainService scheduler_domain_service =
      MakeSchedulerDomainService();
  static const comet::controller::StateAggregateLoader state_aggregate_loader =
      MakeStateAggregateLoader(
          scheduler_domain_service,
          scheduler_view_service);
  static const comet::controller::ReadModelCliService read_model_cli_service =
      MakeReadModelCliService();
  static const comet::controller::SchedulerExecutionSupport scheduler_execution_support({
      [&](const std::vector<comet::HostAssignment>& assignments, const std::string& node_name) {
        return plane_realization_service.FindLatestHostAssignmentForNode(assignments, node_name);
      },
      [&](const std::vector<comet::HostAssignment>& assignments, const std::string& plane_name) {
        return plane_realization_service.FindLatestHostAssignmentForPlane(assignments, plane_name);
      },
      []() { return DefaultArtifactsRoot(); },
      [&](const std::vector<comet::HostObservation>& observations, const std::string& node_name) {
        return runtime_support_service.FindHostObservationForNode(observations, node_name);
      },
      [&](const comet::HostObservation& observation) {
        return runtime_support_service.ParseInstanceRuntimeStatuses(observation);
      },
      [&](const comet::HostObservation& observation) {
        return runtime_support_service.ParseGpuTelemetry(observation);
      },
      [&](const std::string& timestamp_text) {
        return runtime_support_service.TimestampAgeSeconds(timestamp_text);
      },
      []() { return VerificationTimeoutSeconds(); },
      []() { return VerificationStableSamplesRequired(); },
      [&]() { return runtime_support_service.UtcNowSqlTimestamp(); },
  });
  SchedulerService::Deps deps;
  deps.db_path = db_path;
  deps.artifacts_root = artifacts_root;
  deps.default_stale_after_seconds = DefaultStaleAfterSeconds();
  deps.state_aggregate_loader = &state_aggregate_loader;
  deps.scheduler_view_service = &scheduler_view_service;
  deps.event_appender =
      [](comet::ControllerStore& store,
         const std::string& category,
         const std::string& event_type,
         const std::string& message,
         const json& payload,
         const std::string& plane_name,
         const std::string& node_name,
         const std::string& worker_name,
         const std::optional<int>& assignment_id,
         const std::optional<int>& rollout_action_id,
         const std::string& severity) {
        AppendControllerEvent(
            store,
            category,
            event_type,
            message,
            payload,
            plane_name,
            node_name,
            worker_name,
            assignment_id,
            rollout_action_id,
            severity);
      };
  deps.find_rollout_action_by_id =
      [](const std::vector<comet::RolloutActionRecord>& actions, int action_id) {
        return FindRolloutActionById(actions, action_id);
      };
  deps.find_prior_rollout_action_for_worker =
      [&](const std::vector<comet::RolloutActionRecord>& actions,
          const comet::RolloutActionRecord& action,
          const std::string& requested_action_name) {
        return scheduler_execution_support.FindPriorRolloutActionForWorker(
            actions, action, requested_action_name);
      };
  deps.build_eviction_assignments_for_action =
      [&](const comet::DesiredState& desired_state,
          int desired_generation,
          const comet::RolloutActionRecord& action,
          const std::vector<comet::HostAssignment>& existing_assignments) {
        return scheduler_execution_support.BuildEvictionAssignmentsForAction(
            desired_state,
            desired_generation,
            action,
            existing_assignments);
      };
  deps.are_rollout_eviction_assignments_applied =
      [&](const std::vector<comet::HostAssignment>& assignments, int action_id) {
        return scheduler_execution_support.AreRolloutEvictionAssignmentsApplied(
            assignments, action_id);
      };
  deps.mark_workers_evicted =
      [&](comet::ControllerStore* store,
          const std::string& plane_name,
          const std::vector<std::string>& worker_names) {
        scheduler_execution_support.MarkWorkersEvicted(store, plane_name, worker_names);
      };
  deps.materialize_retry_placement_action =
      [&](comet::DesiredState* state,
          const comet::RolloutActionRecord& action,
          const std::vector<std::string>& victim_worker_names) {
        scheduler_execution_support.MaterializeRetryPlacementAction(
            state, action, victim_worker_names);
      };
  deps.materialize_rebalance_plan_entry =
      [&](comet::DesiredState* state, const RebalancePlanEntry& entry) {
        scheduler_execution_support.MaterializeRebalancePlanEntry(state, entry);
      };
  deps.evaluate_scheduling_policy =
      [](const comet::DesiredState& desired_state) {
        return comet::EvaluateSchedulingPolicy(desired_state);
      };
  deps.build_host_assignments =
      [&](const comet::DesiredState& desired_state,
         const std::string& assignment_artifacts_root,
         int desired_generation,
         const std::vector<comet::NodeAvailabilityOverride>& availability_overrides,
         const std::vector<comet::HostObservation>& observations,
         const comet::SchedulingPolicyReport& scheduling_report) {
        return plane_realization_service.BuildHostAssignments(
            desired_state,
            assignment_artifacts_root,
            desired_generation,
            availability_overrides,
            observations,
            scheduling_report);
      };
  deps.build_availability_override_map =
      [&](const std::vector<comet::NodeAvailabilityOverride>& availability_overrides) {
        return runtime_support_service.BuildAvailabilityOverrideMap(availability_overrides);
      };
  deps.print_state_summary =
      [&](const comet::DesiredState& desired_state) {
        controller_print_service.PrintStateSummary(desired_state);
      };
  deps.print_scheduler_decision_summary =
      [&](const comet::DesiredState& desired_state) {
        controller_print_service.PrintSchedulerDecisionSummary(desired_state);
      };
  deps.print_rollout_gate_summary =
      [&](const comet::SchedulingPolicyReport& scheduling_report) {
        controller_print_service.PrintRolloutGateSummary(scheduling_report);
      };
  deps.print_assignment_dispatch_summary =
      [&](const comet::DesiredState& desired_state,
         const std::map<std::string, comet::NodeAvailabilityOverride>&
             availability_override_map,
         const std::vector<comet::HostObservation>& observations,
         int stale_after_seconds) {
        controller_print_service.PrintAssignmentDispatchSummary(
            desired_state,
            availability_override_map,
            observations,
            stale_after_seconds);
      };
  deps.print_persisted_rollout_actions =
      [&](const std::vector<comet::RolloutActionRecord>& actions) {
        controller_print_service.PrintPersistedRolloutActions(actions);
      };
  deps.print_host_assignments =
      [&](const std::vector<comet::HostAssignment>& assignments) {
        controller_print_service.PrintHostAssignments(assignments);
      };
  deps.evaluate_scheduler_action_verification =
      [&](const comet::SchedulerPlaneRuntime& plane_runtime,
          const std::vector<comet::HostObservation>& observations) {
        return scheduler_execution_support.EvaluateSchedulerActionVerification(
            plane_runtime, observations);
      };
  deps.mark_worker_move_verified =
      [&](comet::ControllerStore* store,
          const comet::SchedulerPlaneRuntime& plane_runtime) {
        scheduler_execution_support.MarkWorkerMoveVerified(store, plane_runtime);
      };
  deps.verification_stable_samples_required = []() { return VerificationStableSamplesRequired(); };
  deps.utc_now_sql_timestamp = [&]() { return runtime_support_service.UtcNowSqlTimestamp(); };
  deps.materialize_compose_artifacts =
      [&](const comet::DesiredState& desired_state,
         const std::vector<comet::NodeExecutionPlan>& host_plans) {
        plane_realization_service.MaterializeComposeArtifacts(desired_state, host_plans);
      };
  deps.materialize_infer_runtime_artifact =
      [&](const comet::DesiredState& desired_state,
         const std::string& infer_artifacts_root) {
        plane_realization_service.MaterializeInferRuntimeArtifact(
            desired_state, infer_artifacts_root);
      };
  return SchedulerService(
      [&](const std::optional<std::string>& plane_name,
          const std::optional<std::string>& node_name,
          const std::optional<std::string>& worker_name,
          const std::optional<std::string>& category,
          int limit) {
        return read_model_cli_service.ShowEvents(
            db_path,
            plane_name,
            node_name,
            worker_name,
            category,
            limit);
      },
      std::move(deps));
}

HostRegistryService MakeHostRegistryService(const std::string& db_path) {
  return HostRegistryService(
      db_path,
      [](comet::ControllerStore& store,
         const std::string& event_type,
         const std::string& message,
         const json& payload,
         const std::string& node_name,
         const std::string& severity) {
        AppendControllerEvent(
            store,
            "host-registry",
            event_type,
            message,
            payload,
            "",
            node_name,
            "",
            std::nullopt,
            std::nullopt,
            severity);
      });
}

std::string ResolveDbPath(const std::optional<std::string>& db_arg) {
  return db_arg.value_or(DefaultDbPath());
}

std::optional<comet::RolloutActionRecord> FindRolloutActionById(
    const std::vector<comet::RolloutActionRecord>& actions,
    int action_id) {
  for (const auto& action : actions) {
    if (action.id == action_id) {
      return action;
    }
  }
  return std::nullopt;
}

std::time_t ToUtcTime(std::tm* timestamp) {
#if defined(_WIN32)
  return _mkgmtime(timestamp);
#else
  return timegm(timestamp);
#endif
}

std::optional<long long> HeartbeatAgeSeconds(const std::string& heartbeat_at) {
  if (heartbeat_at.empty()) {
    return std::nullopt;
  }

  std::tm heartbeat_tm{};
  std::istringstream input(heartbeat_at);
  input >> std::get_time(&heartbeat_tm, "%Y-%m-%d %H:%M:%S");
  if (input.fail()) {
    return std::nullopt;
  }

  const std::time_t heartbeat_time = ToUtcTime(&heartbeat_tm);
  if (heartbeat_time < 0) {
    return std::nullopt;
  }

  const std::time_t now = std::time(nullptr);
  return static_cast<long long>(now - heartbeat_time);
}

std::optional<long long> TimestampAgeSeconds(const std::string& timestamp_text) {
  return HeartbeatAgeSeconds(timestamp_text);
}

SchedulerRuntimeView LoadSchedulerRuntimeView(
    comet::ControllerStore& store,
    const std::optional<comet::DesiredState>& desired_state) {
  SchedulerRuntimeView view;
  if (!desired_state.has_value()) {
    return view;
  }
  view.plane_runtime = store.LoadSchedulerPlaneRuntime(desired_state->plane_name);
  for (const auto& runtime : store.LoadSchedulerWorkerRuntimes(desired_state->plane_name)) {
    view.worker_runtime_by_name.emplace(runtime.worker_name, runtime);
  }
  for (const auto& runtime : store.LoadSchedulerNodeRuntimes(desired_state->plane_name)) {
    view.node_runtime_by_name.emplace(runtime.node_name, runtime);
  }
  return view;
}

std::string UtcNowSqlTimestamp() {
  const std::time_t now = std::time(nullptr);
  std::tm tm{};
  if (!comet::platform::GmTime(&now, &tm)) {
    throw std::runtime_error("failed to format current UTC timestamp");
  }
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  return out.str();
}

std::vector<comet::RuntimeProcessStatus> ParseInstanceRuntimeStatuses(
    const comet::HostObservation& observation) {
  if (observation.instance_runtime_json.empty()) {
    return {};
  }
  return comet::DeserializeRuntimeStatusListJson(observation.instance_runtime_json);
}

}  // namespace

class ControllerComponentFactory final {
 public:
  std::unique_ptr<comet::controller::BundleCliService> CreateBundleCliService() const {
    return std::make_unique<comet::controller::BundleCliService>(MakeBundleCliService());
  }

  std::unique_ptr<comet::controller::IRemoteControllerCliService>
  CreateRemoteControllerCliService() const {
    return std::make_unique<comet::controller::RemoteControllerCliService>();
  }

  std::unique_ptr<comet::controller::IReadModelCliService> CreateReadModelCliService() const {
    return std::make_unique<comet::controller::ReadModelCliService>(
        MakeReadModelCliService());
  }

  std::unique_ptr<comet::controller::IHostRegistryService> CreateHostRegistryService(
      const std::string& db_path) const {
    return std::make_unique<HostRegistryService>(MakeHostRegistryService(db_path));
  }

  std::unique_ptr<comet::controller::IPlaneService> CreatePlaneService(
      const std::string& db_path) const {
    return std::make_unique<PlaneService>(MakePlaneService(db_path));
  }

  std::unique_ptr<comet::controller::IAssignmentOrchestrationService>
  CreateAssignmentOrchestrationService() const {
    return std::make_unique<comet::controller::AssignmentOrchestrationService>(
        MakeAssignmentOrchestrationService());
  }

  std::unique_ptr<comet::controller::ISchedulerService> CreateSchedulerService(
      const std::string& db_path,
      const std::string& artifacts_root) const {
    return std::make_unique<SchedulerService>(
        MakeSchedulerService(db_path, artifacts_root));
  }

  std::unique_ptr<comet::controller::IWebUiService> CreateWebUiService(
      const std::string& db_path) const {
    return std::make_unique<WebUiService>(
        db_path,
        [](comet::ControllerStore& store,
           const std::string& event_type,
           const std::string& message,
           const json& payload) {
          AppendControllerEvent(store, "web-ui", event_type, message, payload);
        });
  }

  InteractionHttpService CreateInteractionHttpService() const {
    return MakeInteractionHttpService();
  }

  AuthHttpService CreateAuthHttpService(AuthSupportService& auth_support) const {
    return MakeAuthHttpService(auth_support);
  }

  HostdHttpService CreateHostdHttpService() const {
    return MakeHostdHttpService();
  }

  ModelLibraryService CreateModelLibraryService() const {
    return MakeModelLibraryService();
  }

  ModelLibraryHttpService CreateModelLibraryHttpService(
      const ModelLibraryService& model_library_service) const {
    return MakeModelLibraryHttpService(model_library_service);
  }

  PlaneHttpService CreatePlaneHttpService() const {
    return MakePlaneHttpService();
  }

  comet::controller::ReadModelService CreateReadModelService() const {
    return MakeReadModelService();
  }

  ReadModelHttpService CreateReadModelHttpService(
      const comet::controller::ReadModelService& read_model_service) const {
    return MakeReadModelHttpService(read_model_service);
  }

  SchedulerHttpService CreateSchedulerHttpService(
      const comet::controller::ReadModelService& read_model_service) const {
    return MakeSchedulerHttpService(read_model_service);
  }
};

class ControllerCompositionRoot final : public comet::controller::IControllerServeService {
 public:
  ControllerCompositionRoot(std::string db_path, std::string artifacts_root)
      : db_path_(std::move(db_path)),
        artifacts_root_(std::move(artifacts_root)),
        auth_support_(std::make_unique<AuthSupportService>()),
        bundle_cli_service_(factory_.CreateBundleCliService()),
        read_model_cli_service_(factory_.CreateReadModelCliService()),
        host_registry_service_(factory_.CreateHostRegistryService(db_path_)),
        plane_service_(factory_.CreatePlaneService(db_path_)),
        assignment_orchestration_service_(factory_.CreateAssignmentOrchestrationService()),
        scheduler_service_(factory_.CreateSchedulerService(db_path_, artifacts_root_)),
        web_ui_service_(factory_.CreateWebUiService(db_path_)) {}

  int Serve(
      const std::string& listen_host,
      int listen_port,
      const std::optional<std::string>& requested_ui_root) override {
    ControllerUiService controller_ui_service;
    std::optional<std::filesystem::path> ui_root;
    if (requested_ui_root.has_value()) {
      ui_root = std::filesystem::path(*requested_ui_root);
    } else {
      const std::filesystem::path default_ui_root = controller_ui_service.DefaultUiRoot();
      if (std::filesystem::exists(default_ui_root)) {
        ui_root = default_ui_root;
      }
    }

    auto interaction_http_service = factory_.CreateInteractionHttpService();
    auto auth_http_service = factory_.CreateAuthHttpService(*auth_support_);
    auto hostd_http_service = factory_.CreateHostdHttpService();
    auto bundle_http_service = MakeBundleHttpService(*bundle_cli_service_);
    auto model_library_service = factory_.CreateModelLibraryService();
    auto model_library_http_service =
        factory_.CreateModelLibraryHttpService(model_library_service);
    comet::controller::ControllerHealthService controller_health_service;
    auto plane_http_service = factory_.CreatePlaneHttpService();
    auto read_model_service = factory_.CreateReadModelService();
    auto read_model_http_service =
        factory_.CreateReadModelHttpService(read_model_service);
    auto scheduler_http_service =
        factory_.CreateSchedulerHttpService(read_model_service);

    std::vector<std::unique_ptr<comet::controller::IControllerHttpRouteHandler>>
        pre_auth_handlers;
    pre_auth_handlers.push_back(
        std::make_unique<comet::controller::AuthHttpRouteHandler>(
            auth_http_service));
    pre_auth_handlers.push_back(
        std::make_unique<comet::controller::HostdHttpRouteHandler>(
            hostd_http_service));

    std::vector<std::unique_ptr<comet::controller::IControllerHttpRouteHandler>>
        post_auth_handlers;
    post_auth_handlers.push_back(
        std::make_unique<comet::controller::BundleHttpRouteHandler>(
            bundle_http_service));
    post_auth_handlers.push_back(
        std::make_unique<comet::controller::ModelLibraryHttpRouteHandler>(
            model_library_http_service));
    post_auth_handlers.push_back(
        std::make_unique<comet::controller::PlaneHttpRouteHandler>(
            plane_http_service));
    post_auth_handlers.push_back(
        std::make_unique<comet::controller::ReadModelHttpRouteHandler>(
            read_model_http_service));
    post_auth_handlers.push_back(
        std::make_unique<comet::controller::SchedulerHttpRouteHandler>(
            scheduler_http_service));

    ControllerHttpRouter router(
        db_path_,
        artifacts_root_,
        ui_root,
        *auth_support_,
        interaction_http_service,
        controller_health_service,
        std::move(pre_auth_handlers),
        std::move(post_auth_handlers),
        {
            [&](int status_code,
                const json& payload,
                const std::map<std::string, std::string>& headers) {
              return BuildJsonResponse(status_code, payload, headers);
            },
            [&](const std::filesystem::path& root,
                const std::string& request_path) {
              return controller_ui_service.ResolveRequestPath(root, request_path);
            },
            [&](const std::filesystem::path& file_path) {
              return controller_ui_service.BuildStaticFileResponse(file_path);
            },
            [&](const std::string& action_db_path, int assignment_id) {
              return assignment_orchestration_service_
                  ->ExecuteRetryHostAssignmentAction(
                      action_db_path,
                      assignment_id);
            },
        });
    ControllerHttpServer server({
        [&](const HttpRequest& request) {
          const ScopedCurrentHttpRequest scoped_request(request);
          return router.HandleRequest(request);
        },
        [&](SocketHandle client_fd,
           const std::string& interaction_db_path,
           const HttpRequest& request) {
          interaction_http_service.StreamPlaneInteractionSse(
              client_fd,
              interaction_db_path,
              request);
        },
        [](const std::string& method, const std::string& path) {
          return comet::controller::ParseInteractionStreamPlaneName(method, path);
        },
        [&](const comet::EventRecord& event) {
          return read_model_service.BuildEventPayloadItem(event);
        },
    });
    return server.Serve({
        db_path_,
        artifacts_root_,
        listen_host,
        listen_port,
        ui_root,
        "/health,/api/v1/health,/api/v1/bundles/validate,/api/v1/bundles/preview,/api/v1/bundles/import,/api/v1/bundles/apply,/api/v1/model-library,/api/v1/model-library/download,/api/v1/planes,/api/v1/planes/<plane>,/api/v1/planes/<plane>/dashboard,/api/v1/planes/<plane>/start,/api/v1/planes/<plane>/stop,/api/v1/planes/<plane>[DELETE],/api/v1/planes/<plane>/interaction/status,/api/v1/planes/<plane>/interaction/models,/api/v1/planes/<plane>/interaction/chat/completions,/api/v1/planes/<plane>/interaction/chat/completions/stream,/api/v1/state,/api/v1/dashboard,/api/v1/host-assignments,/api/v1/host-observations,/api/v1/host-health,/api/v1/disk-state,/api/v1/rollout-actions,/api/v1/rebalance-plan,/api/v1/events,/api/v1/events/stream,/api/v1/scheduler-tick,/api/v1/reconcile-rebalance-proposals,/api/v1/reconcile-rollout-actions,/api/v1/apply-rebalance-proposal,/api/v1/set-rollout-action-status,/api/v1/enqueue-rollout-eviction,/api/v1/apply-ready-rollout-action,/api/v1/node-availability,/api/v1/retry-host-assignment,/api/v1/hostd/hosts,/api/v1/hostd/hosts/<node>/revoke,/api/v1/hostd/hosts/<node>/rotate-key",
    });
  }

  ControllerCli BuildCli(const ControllerCommandLine& cli) {
    return ControllerCli(
        cli,
        *host_registry_service_,
        *plane_service_,
        *scheduler_service_,
        *web_ui_service_,
        *bundle_cli_service_,
        *read_model_cli_service_,
        *assignment_orchestration_service_,
        *this);
  }

 private:
  ControllerComponentFactory factory_;
  std::string db_path_;
  std::string artifacts_root_;
  std::unique_ptr<AuthSupportService> auth_support_;
  std::unique_ptr<comet::controller::BundleCliService> bundle_cli_service_;
  std::unique_ptr<comet::controller::IReadModelCliService> read_model_cli_service_;
  std::unique_ptr<comet::controller::IHostRegistryService> host_registry_service_;
  std::unique_ptr<comet::controller::IPlaneService> plane_service_;
  std::unique_ptr<comet::controller::IAssignmentOrchestrationService>
      assignment_orchestration_service_;
  std::unique_ptr<comet::controller::ISchedulerService> scheduler_service_;
  std::unique_ptr<comet::controller::IWebUiService> web_ui_service_;
};

class ControllerApp final {
 public:
  ControllerApp(int argc, char** argv) : cli_(argc, argv) {}

  int Run() {
    if (!cli_.HasCommand()) {
      cli_.PrintUsage(std::cout);
      return 1;
    }

    ControllerComponentFactory factory;
    const auto bundle_cli_service = factory.CreateBundleCliService();
    const std::string& command = cli_.command();
    if (command == "show-demo-plan") {
      bundle_cli_service->ShowDemoPlan();
      return 0;
    }

    if (command == "render-demo-compose") {
      return bundle_cli_service->RenderDemoCompose(cli_.node());
    }

    try {
      const auto db_arg = cli_.db();
      const auto controller_target = ResolveControllerTarget(cli_.controller(), db_arg);
      auto remote_controller_cli_service = factory.CreateRemoteControllerCliService();
      if (controller_target.has_value()) {
        return remote_controller_cli_service->ExecuteCommand(
            ParseControllerEndpointTarget(*controller_target),
            command,
            cli_);
      }

      const std::string db_path = ResolveDbPath(db_arg);
      const comet::controller::ControllerRequestSupport request_support;
      ControllerCompositionRoot composition_root(
          db_path,
          request_support.ResolveArtifactsRoot(
              cli_.artifacts_root(),
              DefaultArtifactsRoot()));
      ControllerCli controller_cli = composition_root.BuildCli(cli_);

      if (const auto result = controller_cli.TryRun(); result.has_value()) {
        return *result;
      }

    } catch (const std::exception& error) {
      std::cerr << "error: " << error.what() << "\n";
      return 1;
    }

    cli_.PrintUsage(std::cout);
    return 1;
  }

 private:
  ControllerCommandLine cli_;
};

namespace comet::controller {

int RunControllerApp(int argc, char** argv) {
  ControllerApp app(argc, argv);
  return app.Run();
}

}  // namespace comet::controller
