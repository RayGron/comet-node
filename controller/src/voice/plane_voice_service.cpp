#include "voice/plane_voice_service.h"

#include <algorithm>
#include <chrono>
#include <thread>

#include "naim/security/crypto_utils.h"
#include "naim/state/sqlite_store.h"

namespace naim::controller {

namespace {

using nlohmann::json;

std::string NormalizeControllerTargetHost(const PublishedPort& port) {
  if (port.host_ip.empty() || port.host_ip == "0.0.0.0") {
    return "127.0.0.1";
  }
  return port.host_ip;
}

bool IsControllerLocalNode(const std::string& node_name) {
  return node_name.empty() || node_name == "local-hostd" ||
         node_name == "controller-local";
}

bool IsLoopbackTargetHost(const std::string& host) {
  return host == "127.0.0.1" || host == "localhost" || host == "::1";
}

const InstanceSpec* FindVoiceInstance(const DesiredState& desired_state) {
  const auto it = std::find_if(
      desired_state.instances.begin(),
      desired_state.instances.end(),
      [](const InstanceSpec& instance) {
        return instance.role == InstanceRole::VoiceModule;
      });
  if (it == desired_state.instances.end()) {
    return nullptr;
  }
  return &*it;
}

std::string NormalizeVoicePathSuffix(const std::string& path_suffix) {
  if (path_suffix.empty() || path_suffix == "/" || path_suffix == "/transcribe") {
    return "/v1/transcribe";
  }
  if (path_suffix == "/health" || path_suffix == "/v1/transcribe" ||
      path_suffix == "/api/asr/transcribe") {
    return path_suffix;
  }
  if (path_suffix.front() == '/') {
    return path_suffix;
  }
  return "/" + path_suffix;
}

json BuildHeaderArray(
    const std::vector<std::pair<std::string, std::string>>& headers) {
  json result = json::array();
  for (const auto& [key, value] : headers) {
    result.push_back(json::array({key, value}));
  }
  return result;
}

std::string EncodeBodyBase64(const std::string& body) {
  if (body.empty()) {
    return "";
  }
  return naim::EncodeBytesBase64(
      std::vector<unsigned char>(body.begin(), body.end()));
}

HttpResponse BuildProxyErrorResponse(
    int status_code,
    const std::string& code,
    const std::string& message) {
  HttpResponse response;
  response.status_code = status_code;
  response.content_type = "application/json";
  response.headers["Content-Type"] = response.content_type;
  response.body =
      json{{"status", "error"}, {"code", code}, {"message", message}}.dump();
  return response;
}

HttpResponse ParseProxyResponsePayload(const json& progress) {
  if (!progress.is_object() || !progress.contains("response") ||
      !progress.at("response").is_object()) {
    return BuildProxyErrorResponse(
        502,
        "voice_runtime_proxy_response_missing",
        "hostd voice runtime proxy did not return an upstream response");
  }
  const auto& response_payload = progress.at("response");
  HttpResponse response;
  response.status_code = response_payload.value("status_code", 502);
  response.content_type =
      response_payload.value("content_type", std::string("application/json"));
  response.body = response_payload.value("body", std::string{});
  if (response_payload.contains("headers") &&
      response_payload.at("headers").is_object()) {
    for (const auto& [key, value] : response_payload.at("headers").items()) {
      if (value.is_string()) {
        response.headers[key] = value.get<std::string>();
      }
    }
  }
  if (!response.content_type.empty()) {
    response.headers["Content-Type"] = response.content_type;
  }
  return response;
}

HttpResponse SendPlaneVoiceRequest(
    const std::string& db_path,
    const std::string& plane_name,
    const ControllerEndpointTarget& target,
    const std::string& method,
    const std::string& path,
    const std::string& body,
    const std::vector<std::pair<std::string, std::string>>& headers) {
  if (!target.route_via_hostd_proxy) {
    return SendControllerHttpRequest(target, method, path, body, headers);
  }
  if (db_path.empty() || target.node_name.empty()) {
    return BuildProxyErrorResponse(
        502,
        "voice_runtime_proxy_route_missing",
        "voice runtime target requires hostd proxy routing but has no node binding");
  }
  if (!IsLoopbackTargetHost(target.host)) {
    return BuildProxyErrorResponse(
        502,
        "voice_runtime_proxy_target_rejected",
        "hostd voice runtime proxy only accepts node-local loopback targets");
  }

  ControllerStore store(db_path);
  store.Initialize();
  const std::string request_id =
      "voice-" + plane_name + "-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::string proxy_plane_name =
      "runtime-http-proxy:voice-listener:" + plane_name + ":" + request_id;

  HostAssignment assignment;
  assignment.node_name = target.node_name;
  assignment.plane_name = proxy_plane_name;
  assignment.desired_generation = 0;
  assignment.max_attempts = 3;
  assignment.assignment_type = "runtime-http-proxy";
  assignment.desired_state_json =
      json{
          {"target_host", target.host},
          {"target_port", target.port},
          {"method", method},
          {"path", path},
          {"body_base64", EncodeBodyBase64(body)},
          {"headers", BuildHeaderArray(headers)},
          {"request_id", request_id},
          {"policy", "voice-listener"},
      }
          .dump();
  assignment.artifacts_root = "";
  assignment.status = HostAssignmentStatus::Pending;
  assignment.status_message = "proxy voice runtime HTTP request";
  store.EnqueueHostAssignments(
      {assignment}, "superseded voice runtime proxy request");

  const auto assignments = store.LoadHostAssignments(
      target.node_name,
      std::nullopt,
      proxy_plane_name);
  if (assignments.empty()) {
    return BuildProxyErrorResponse(
        500,
        "voice_runtime_proxy_queue_failed",
        "failed to queue hostd voice runtime proxy assignment");
  }
  const int assignment_id = assignments.back().id;
  const auto started_at = std::chrono::steady_clock::now();
  while (std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - started_at)
             .count() < 120000) {
    const auto current = store.LoadHostAssignment(assignment_id);
    if (!current.has_value()) {
      return BuildProxyErrorResponse(
          500,
          "voice_runtime_proxy_assignment_missing",
          "hostd voice runtime proxy assignment disappeared");
    }
    if (current->status == HostAssignmentStatus::Applied) {
      const auto progress = json::parse(current->progress_json, nullptr, false);
      return ParseProxyResponsePayload(progress);
    }
    if (current->status == HostAssignmentStatus::Failed) {
      return BuildProxyErrorResponse(
          502,
          "voice_runtime_proxy_failed",
          current->status_message.empty()
              ? "hostd voice runtime proxy request failed"
              : current->status_message);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return BuildProxyErrorResponse(
      504,
      "voice_runtime_proxy_timeout",
      "timed out waiting for hostd voice runtime proxy response");
}

bool ProbeVoiceTargetOk(
    const std::string& db_path,
    const std::string& plane_name,
    const ControllerEndpointTarget& target) {
  try {
    const auto response =
        SendPlaneVoiceRequest(db_path, plane_name, target, "GET", "/health", "", {});
    return response.status_code >= 200 && response.status_code < 300;
  } catch (...) {
    return false;
  }
}

}  // namespace

bool PlaneVoiceService::IsEnabled(const DesiredState& desired_state) const {
  return desired_state.voice_listener.has_value() &&
         desired_state.voice_listener->enabled;
}

std::optional<ControllerEndpointTarget> PlaneVoiceService::ResolveTarget(
    const DesiredState& desired_state) const {
  const auto* voice = FindVoiceInstance(desired_state);
  if (voice == nullptr) {
    return std::nullopt;
  }
  const auto published = std::find_if(
      voice->published_ports.begin(),
      voice->published_ports.end(),
      [](const PublishedPort& port) { return port.host_port > 0; });
  if (published == voice->published_ports.end()) {
    return std::nullopt;
  }
  ControllerEndpointTarget target;
  target.host = NormalizeControllerTargetHost(*published);
  target.port = published->host_port;
  target.raw = "http://" + target.host + ":" + std::to_string(target.port);
  target.node_name = voice->node_name;
  if (!IsControllerLocalNode(target.node_name) && IsLoopbackTargetHost(target.host)) {
    target.route_via_hostd_proxy = true;
    target.route_mode = "hostd-runtime-proxy";
  }
  return target;
}

nlohmann::json PlaneVoiceService::BuildStatusPayload(
    const std::string& db_path,
    const DesiredState& desired_state,
    const std::optional<std::string>& plane_state) const {
  const bool enabled = IsEnabled(desired_state);
  const auto* voice_instance = FindVoiceInstance(desired_state);
  const auto target = ResolveTarget(desired_state);
  const bool running_plane = plane_state.has_value() && *plane_state == "running";
  const bool should_probe_runtime =
      enabled && running_plane && target.has_value() && !target->route_via_hostd_proxy;
  const bool ready =
      enabled && running_plane && target.has_value() &&
      (target->route_via_hostd_proxy ||
       ProbeVoiceTargetOk(db_path, desired_state.plane_name, *target));

  std::string reason = "ready";
  if (!enabled) {
    reason = "voice_listener_disabled";
  } else if (!running_plane) {
    reason = "plane_not_running";
  } else if (!target.has_value()) {
    reason = "target_missing";
  } else if (!ready) {
    reason = "target_unreachable";
  }

  nlohmann::json payload = {
      {"status", "ok"},
      {"voice_listener_enabled", enabled},
      {"voice_listener_ready", ready},
      {"reason", reason},
      {"plane_name", desired_state.plane_name},
      {"plane_state", plane_state.has_value() ? nlohmann::json(*plane_state)
                                               : nlohmann::json(nullptr)},
      {"voice_container_name",
       voice_instance != nullptr ? nlohmann::json(voice_instance->name)
                                  : nlohmann::json(nullptr)},
      {"voice_target", target.has_value() ? nlohmann::json(target->raw)
                                           : nlohmann::json(nullptr)},
      {"voice_node_name",
       target.has_value() && !target->node_name.empty()
           ? nlohmann::json(target->node_name)
           : nlohmann::json(nullptr)},
      {"voice_route_mode",
       target.has_value() ? nlohmann::json(target->route_mode)
                           : nlohmann::json(nullptr)},
      {"wake_phrase",
       desired_state.voice_listener.has_value()
           ? nlohmann::json(desired_state.voice_listener->wake_phrase)
           : nlohmann::json(nullptr)},
      {"language",
       desired_state.voice_listener.has_value()
           ? nlohmann::json(desired_state.voice_listener->language)
           : nlohmann::json(nullptr)},
      {"model_mount_path",
       desired_state.voice_listener.has_value()
           ? nlohmann::json(desired_state.voice_listener->model.mount_path)
           : nlohmann::json(nullptr)},
  };

  if (ready && should_probe_runtime) {
    try {
      const auto response = SendPlaneVoiceRequest(
          db_path,
          desired_state.plane_name,
          *target,
          "GET",
          "/health",
          "",
          {});
      if (!response.body.empty()) {
        const auto runtime_status = nlohmann::json::parse(response.body, nullptr, false);
        if (runtime_status.is_object()) {
          payload["runtime"] = runtime_status;
        }
      }
    } catch (...) {
    }
  }
  return payload;
}

std::optional<HttpResponse> PlaneVoiceService::ProxyPlaneVoiceRequest(
    const std::string& db_path,
    const DesiredState& desired_state,
    const std::string& method,
    const std::string& path_suffix,
    const std::string& body,
    const std::vector<std::pair<std::string, std::string>>& headers,
    std::string* error_code,
    std::string* error_message) const {
  if (!IsEnabled(desired_state)) {
    if (error_code != nullptr) {
      *error_code = "voice_listener_disabled";
    }
    if (error_message != nullptr) {
      *error_message = "voice listener is not enabled for this plane";
    }
    return std::nullopt;
  }
  const auto target = ResolveTarget(desired_state);
  if (!target.has_value()) {
    if (error_code != nullptr) {
      *error_code = "voice_listener_target_missing";
    }
    if (error_message != nullptr) {
      *error_message = "voice listener service target is not available";
    }
    return std::nullopt;
  }

  try {
    return SendPlaneVoiceRequest(
        db_path,
        desired_state.plane_name,
        *target,
        method,
        NormalizeVoicePathSuffix(path_suffix),
        body,
        headers);
  } catch (const std::exception& error) {
    if (error_code != nullptr) {
      *error_code = "voice_listener_upstream_failed";
    }
    if (error_message != nullptr) {
      *error_message = error.what();
    }
    return std::nullopt;
  }
}

}  // namespace naim::controller
