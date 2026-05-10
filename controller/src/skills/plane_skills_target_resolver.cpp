#include "skills/plane_skills_target_resolver.h"

#include <algorithm>
#include <chrono>
#include <thread>

#include <nlohmann/json.hpp>

#include "http/controller_http_transport.h"
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

json BuildHeaderArray(
    const std::vector<std::pair<std::string, std::string>>& headers) {
  json result = json::array();
  for (const auto& [key, value] : headers) {
    result.push_back(json::array({key, value}));
  }
  return result;
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
        "skills_runtime_proxy_response_missing",
        "hostd skills runtime proxy did not return an upstream response");
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

}  // namespace

std::vector<std::pair<std::string, std::string>>
PlaneSkillsTargetResolver::DefaultJsonHeaders() {
  return {{"Content-Type", "application/json"}};
}

const InstanceSpec* PlaneSkillsTargetResolver::FindSkillsInstance(
    const DesiredState& desired_state) {
  const auto it = std::find_if(
      desired_state.instances.begin(),
      desired_state.instances.end(),
      [](const InstanceSpec& instance) { return instance.role == InstanceRole::Skills; });
  if (it == desired_state.instances.end()) {
    return nullptr;
  }
  return &*it;
}

std::optional<ControllerEndpointTarget>
PlaneSkillsTargetResolver::ResolvePlaneLocalTarget(
    const DesiredState& desired_state) {
  const auto* skills = FindSkillsInstance(desired_state);
  if (skills == nullptr) {
    return std::nullopt;
  }
  const auto published = std::find_if(
      skills->published_ports.begin(),
      skills->published_ports.end(),
      [](const PublishedPort& port) { return port.host_port > 0; });
  if (published == skills->published_ports.end()) {
    return std::nullopt;
  }
  ControllerEndpointTarget target;
  target.host = NormalizeControllerTargetHost(*published);
  target.port = published->host_port;
  target.raw = "http://" + target.host + ":" + std::to_string(target.port);
  target.node_name = skills->node_name;
  if (!IsControllerLocalNode(target.node_name) && IsLoopbackTargetHost(target.host)) {
    target.route_via_hostd_proxy = true;
    target.route_mode = "hostd-runtime-proxy";
  }
  return target;
}

::HttpResponse PlaneSkillsTargetResolver::SendPlaneLocalRequest(
    const std::string& db_path,
    const std::string& plane_name,
    const ControllerEndpointTarget& target,
    const std::string& method,
    const std::string& path,
    const std::string& body,
    const std::vector<std::pair<std::string, std::string>>& headers,
    int timeout_ms) {
  if (!target.route_via_hostd_proxy) {
    return SendControllerHttpRequest(target, method, path, body, headers);
  }
  if (db_path.empty() || target.node_name.empty()) {
    return BuildProxyErrorResponse(
        502,
        "skills_runtime_proxy_route_missing",
        "skills runtime target requires hostd proxy routing but has no node binding");
  }
  if (!IsLoopbackTargetHost(target.host)) {
    return BuildProxyErrorResponse(
        502,
        "skills_runtime_proxy_target_rejected",
        "hostd skills runtime proxy only accepts node-local loopback targets");
  }

  ControllerStore store(db_path);
  store.Initialize();
  const std::string request_id =
      "skills-" + plane_name + "-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::string proxy_plane_name =
      "runtime-http-proxy:skills:" + plane_name + ":" + request_id;

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
          {"body", body},
          {"headers", BuildHeaderArray(headers)},
          {"request_id", request_id},
          {"policy", "skills"},
      }
          .dump();
  assignment.artifacts_root = "";
  assignment.status = HostAssignmentStatus::Pending;
  assignment.status_message = "proxy skills runtime HTTP request";
  store.EnqueueHostAssignments({assignment}, "superseded skills runtime proxy request");

  const auto assignments = store.LoadHostAssignments(
      target.node_name,
      std::nullopt,
      proxy_plane_name);
  if (assignments.empty()) {
    return BuildProxyErrorResponse(
        500,
        "skills_runtime_proxy_queue_failed",
        "failed to queue hostd skills runtime proxy assignment");
  }
  const int assignment_id = assignments.back().id;
  const auto started_at = std::chrono::steady_clock::now();
  while (std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - started_at)
             .count() < timeout_ms) {
    const auto current = store.LoadHostAssignment(assignment_id);
    if (!current.has_value()) {
      return BuildProxyErrorResponse(
          500,
          "skills_runtime_proxy_assignment_missing",
          "hostd skills runtime proxy assignment disappeared");
    }
    if (current->status == HostAssignmentStatus::Applied) {
      const auto progress = json::parse(current->progress_json, nullptr, false);
      return ParseProxyResponsePayload(progress);
    }
    if (current->status == HostAssignmentStatus::Failed) {
      return BuildProxyErrorResponse(
          502,
          "skills_runtime_proxy_failed",
          current->status_message.empty()
              ? "hostd skills runtime proxy request failed"
              : current->status_message);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return BuildProxyErrorResponse(
      504,
      "skills_runtime_proxy_timeout",
      "timed out waiting for hostd skills runtime proxy response");
}

std::string PlaneSkillsTargetResolver::NormalizeSkillPathSuffix(
    const std::string& path_suffix) {
  if (path_suffix.empty() || path_suffix == "/") {
    return "/v1/skills";
  }
  if (path_suffix.front() == '/') {
    return "/v1/skills" + path_suffix;
  }
  return "/v1/skills/" + path_suffix;
}

}  // namespace naim::controller
