#include "backend/http_hostd_backend.h"

#include <ctime>
#include <stdexcept>

#include "comet/security/crypto_utils.h"

namespace comet::hostd {

HttpHostdBackend::HttpHostdBackend(
    std::string controller_url,
    std::string private_key_base64,
    std::string trusted_controller_fingerprint,
    const IHttpHostdBackendSupport& support)
    : controller_url_(std::move(controller_url)),
      private_key_base64_(std::move(private_key_base64)),
      trusted_controller_fingerprint_(std::move(trusted_controller_fingerprint)),
      support_(support) {}

std::optional<comet::HostAssignment> HttpHostdBackend::ClaimNextHostAssignment(
    const std::string& node_name) {
  EnsureSession(node_name, "claiming next assignment");
  const auto payload = SendEncryptedControllerJsonRequest(
      "/api/v1/hostd/assignments/next",
      nlohmann::json{{"node_name", node_name}},
      "assignments/next");
  if (!payload.contains("assignment") || payload["assignment"].is_null()) {
    return std::nullopt;
  }
  return support_.ParseAssignmentPayload(payload["assignment"]);
}

bool HttpHostdBackend::TransitionClaimedHostAssignment(
    const int assignment_id,
    const comet::HostAssignmentStatus status,
    const std::string& status_message) {
  if (status == comet::HostAssignmentStatus::Applied) {
    SendEncryptedControllerJsonRequest(
        "/api/v1/hostd/assignments/" + std::to_string(assignment_id) + "/applied",
        nlohmann::json{{"status_message", status_message}},
        "assignments/" + std::to_string(assignment_id) + "/applied");
    return true;
  }
  if (status == comet::HostAssignmentStatus::Pending ||
      status == comet::HostAssignmentStatus::Failed) {
    SendEncryptedControllerJsonRequest(
        "/api/v1/hostd/assignments/" + std::to_string(assignment_id) + "/failed",
        nlohmann::json{
            {"status_message", status_message},
            {"retry", status == comet::HostAssignmentStatus::Pending},
        },
        "assignments/" + std::to_string(assignment_id) + "/failed");
    return true;
  }
  throw std::runtime_error("unsupported remote assignment transition");
}

bool HttpHostdBackend::UpdateHostAssignmentProgress(
    const int assignment_id,
    const nlohmann::json& progress) {
  SendEncryptedControllerJsonRequest(
      "/api/v1/hostd/assignments/" + std::to_string(assignment_id) + "/progress",
      progress,
      "assignments/" + std::to_string(assignment_id) + "/progress");
  return true;
}

void HttpHostdBackend::UpsertHostObservation(const comet::HostObservation& observation) {
  EnsureSession(observation.node_name, "uploading observation");
  SendEncryptedControllerJsonRequest(
      "/api/v1/hostd/observations",
      support_.BuildHostObservationPayload(observation),
      "observations/upsert");
}

void HttpHostdBackend::AppendEvent(const comet::EventRecord& event) {
  if (!event.node_name.empty()) {
    EnsureSession(event.node_name, "appending event");
  }
  SendEncryptedControllerJsonRequest(
      "/api/v1/hostd/events",
      nlohmann::json{
          {"plane_name", event.plane_name},
          {"node_name", event.node_name},
          {"worker_name", event.worker_name},
          {"assignment_id", event.assignment_id.has_value() ? nlohmann::json(*event.assignment_id)
                                                            : nlohmann::json(nullptr)},
          {"rollout_action_id",
           event.rollout_action_id.has_value() ? nlohmann::json(*event.rollout_action_id)
                                               : nlohmann::json(nullptr)},
          {"category", event.category},
          {"event_type", event.event_type},
          {"severity", event.severity},
          {"message", event.message},
          {"payload_json", event.payload_json},
      },
      "events/append");
}

void HttpHostdBackend::UpsertDiskRuntimeState(const comet::DiskRuntimeState& state) {
  EnsureSession(state.node_name, "upserting disk runtime state");
  SendEncryptedControllerJsonRequest(
      "/api/v1/hostd/disk-runtime-state",
      support_.BuildDiskRuntimeStatePayload(state),
      "disk-runtime-state/upsert");
}

std::optional<comet::DiskRuntimeState> HttpHostdBackend::LoadDiskRuntimeState(
    const std::string& disk_name,
    const std::string& node_name) {
  EnsureSession(node_name, "loading disk runtime state");
  const auto payload = SendEncryptedControllerJsonRequest(
      "/api/v1/hostd/disk-runtime-state/load",
      nlohmann::json{{"disk_name", disk_name}, {"node_name", node_name}},
      "disk-runtime-state/load");
  if (!payload.contains("runtime_state") || payload["runtime_state"].is_null()) {
    return std::nullopt;
  }
  return support_.ParseDiskRuntimeStatePayload(payload["runtime_state"]);
}

bool HttpHostdBackend::IsRecoverableSessionErrorMessage(const std::string& message) {
  return message.find("invalid or missing host session") != std::string::npos ||
         message.find("stale or replayed host session request") != std::string::npos;
}

void HttpHostdBackend::ResetSessionState() {
  session_token_.clear();
  session_node_name_.clear();
  host_sequence_ = 0;
  controller_sequence_ = 0;
}

std::string HttpHostdBackend::BuildRequestAad(
    const std::string& message_type,
    const std::uint64_t sequence_number) const {
  return "request\n" + message_type + "\n" + session_node_name_ + "\n" +
         std::to_string(sequence_number);
}

std::string HttpHostdBackend::BuildResponseAad(
    const std::string& message_type,
    const std::uint64_t sequence_number) const {
  return "response\n" + message_type + "\n" + session_node_name_ + "\n" +
         std::to_string(sequence_number);
}

nlohmann::json HttpHostdBackend::SendEncryptedControllerJsonRequest(
    const std::string& path,
    const nlohmann::json& payload,
    const std::string& message_type) {
  return RetryOnRecoverableSessionError(
      message_type,
      "recovering host session",
      [&]() {
        if (session_token_.empty()) {
          throw std::runtime_error("missing host session token");
        }
        host_sequence_ += 1;
        const comet::EncryptedEnvelope envelope = comet::EncryptEnvelopeBase64(
            payload.dump(),
            session_token_,
            BuildRequestAad(message_type, host_sequence_));
        const auto response = support_.SendControllerJsonRequest(
            controller_url_,
            "POST",
            path,
            nlohmann::json{
                {"encrypted", true},
                {"sequence_number", host_sequence_},
                {"nonce", envelope.nonce_base64},
                {"ciphertext", envelope.ciphertext_base64},
            },
            SessionHeaders());
        if (!response.value("encrypted", false)) {
          return response;
        }
        const std::uint64_t controller_sequence =
            response.value("sequence_number", static_cast<std::uint64_t>(0));
        if (controller_sequence <= controller_sequence_) {
          throw std::runtime_error("stale or replayed controller session response");
        }
        const comet::EncryptedEnvelope response_envelope{
            response.value("nonce", std::string{}),
            response.value("ciphertext", std::string{}),
        };
        const std::string decrypted = comet::DecryptEnvelopeBase64(
            response_envelope,
            session_token_,
            BuildResponseAad(message_type, controller_sequence));
        controller_sequence_ = controller_sequence;
        return decrypted.empty() ? nlohmann::json::object() : nlohmann::json::parse(decrypted);
      });
}

void HttpHostdBackend::EnsureSession(const std::string& node_name, const std::string& status_message) {
  if (!session_token_.empty() &&
      (host_sequence_ >= SessionRekeyMessageLimit() ||
       controller_sequence_ >= SessionRekeyMessageLimit())) {
    ResetSessionState();
  }
  if (!session_token_.empty() && session_node_name_ == node_name) {
    try {
      SendEncryptedControllerJsonRequest(
          "/api/v1/hostd/session/heartbeat",
          nlohmann::json{
              {"node_name", node_name},
              {"session_state", "connected"},
              {"status_message", status_message}},
          "session/heartbeat");
      return;
    } catch (const std::exception&) {
      ResetSessionState();
    }
  }
  const std::string nonce = comet::RandomTokenBase64(24);
  const std::string timestamp = std::to_string(std::time(nullptr));
  const std::string message = "hostd-session-open\n" + node_name + "\n" + timestamp + "\n" + nonce;
  const std::string signature = comet::SignDetachedBase64(message, private_key_base64_);
  const auto response = support_.SendControllerJsonRequest(
      controller_url_,
      "POST",
      "/api/v1/hostd/session/open",
      nlohmann::json{
          {"node_name", node_name},
          {"timestamp", timestamp},
          {"nonce", nonce},
          {"signature", signature},
          {"status_message", status_message},
      });
  const std::string controller_fingerprint =
      response.value("controller_public_key_fingerprint", std::string{});
  if (!trusted_controller_fingerprint_.empty() &&
      controller_fingerprint != trusted_controller_fingerprint_) {
    throw std::runtime_error("controller fingerprint mismatch during host session open");
  }
  session_token_ = response.value("session_token", std::string{});
  session_node_name_ = node_name;
  host_sequence_ = 0;
  controller_sequence_ =
      response.value("controller_sequence", static_cast<std::uint64_t>(0));
  SendEncryptedControllerJsonRequest(
      "/api/v1/hostd/session/heartbeat",
      nlohmann::json{
          {"node_name", node_name},
          {"session_state", "connected"},
          {"status_message", status_message}},
      "session/heartbeat");
}

std::map<std::string, std::string> HttpHostdBackend::SessionHeaders() const {
  if (session_token_.empty()) {
    return {};
  }
  return {
      {"X-Comet-Host-Session", session_token_},
      {"X-Comet-Host-Node", session_node_name_},
  };
}

template <typename Fn>
nlohmann::json HttpHostdBackend::RetryOnRecoverableSessionError(
    const std::string& message_type,
    const char* recovery_status_message,
    Fn&& fn) {
  try {
    return fn();
  } catch (const std::exception& error) {
    if (session_node_name_.empty() ||
        message_type == "session/heartbeat" ||
        !IsRecoverableSessionErrorMessage(error.what())) {
      throw;
    }
    const std::string node_name = session_node_name_;
    ResetSessionState();
    EnsureSession(node_name, recovery_status_message);
    return fn();
  }
}

}  // namespace comet::hostd
