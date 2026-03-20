#include "comet/runtime_status.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace comet {

namespace {

using nlohmann::json;

json ToJson(const RuntimeStatus& status) {
  return json{
      {"plane_name", status.plane_name},
      {"control_root", status.control_root},
      {"controller_url", status.controller_url},
      {"primary_infer_node", status.primary_infer_node},
      {"runtime_backend", status.runtime_backend},
      {"runtime_phase", status.runtime_phase},
      {"enabled_gpu_nodes", status.enabled_gpu_nodes},
      {"registry_entries", status.registry_entries},
      {"supervisor_pid", status.supervisor_pid},
      {"aliases", status.aliases},
      {"active_model_id", status.active_model_id},
      {"active_served_model_name", status.active_served_model_name},
      {"active_runtime_profile", status.active_runtime_profile},
      {"cached_local_model_path", status.cached_local_model_path},
      {"gateway_listen", status.gateway_listen},
      {"upstream_models_url", status.upstream_models_url},
      {"inference_health_url", status.inference_health_url},
      {"gateway_health_url", status.gateway_health_url},
      {"started_at", status.started_at},
      {"active_model_ready", status.active_model_ready},
      {"gateway_plan_ready", status.gateway_plan_ready},
      {"inference_ready", status.inference_ready},
      {"gateway_ready", status.gateway_ready},
      {"launch_ready", status.launch_ready},
  };
}

RuntimeStatus RuntimeStatusFromJson(const json& value) {
  RuntimeStatus status;
  status.plane_name = value.value("plane_name", std::string{});
  status.control_root = value.value("control_root", std::string{});
  status.controller_url = value.value("controller_url", std::string{});
  status.primary_infer_node = value.value("primary_infer_node", std::string{});
  status.runtime_backend = value.value("runtime_backend", std::string{});
  status.runtime_phase = value.value("runtime_phase", std::string{});
  status.enabled_gpu_nodes = value.value("enabled_gpu_nodes", 0);
  status.registry_entries = value.value("registry_entries", 0);
  status.supervisor_pid = value.value("supervisor_pid", 0);
  status.aliases = value.value("aliases", std::vector<std::string>{});
  status.active_model_id = value.value("active_model_id", std::string{});
  status.active_served_model_name = value.value("active_served_model_name", std::string{});
  status.active_runtime_profile = value.value("active_runtime_profile", std::string{});
  status.cached_local_model_path = value.value("cached_local_model_path", std::string{});
  status.gateway_listen = value.value("gateway_listen", std::string{});
  status.upstream_models_url = value.value("upstream_models_url", std::string{});
  status.inference_health_url = value.value("inference_health_url", std::string{});
  status.gateway_health_url = value.value("gateway_health_url", std::string{});
  status.started_at = value.value("started_at", std::string{});
  status.active_model_ready = value.value("active_model_ready", false);
  status.gateway_plan_ready = value.value("gateway_plan_ready", false);
  status.inference_ready = value.value("inference_ready", false);
  status.gateway_ready = value.value("gateway_ready", false);
  status.launch_ready = value.value("launch_ready", false);
  return status;
}

}  // namespace

std::string SerializeRuntimeStatusJson(const RuntimeStatus& status) {
  return ToJson(status).dump(2);
}

RuntimeStatus DeserializeRuntimeStatusJson(const std::string& json_text) {
  return RuntimeStatusFromJson(json::parse(json_text));
}

std::optional<RuntimeStatus> LoadRuntimeStatusJson(const std::string& path) {
  if (!std::filesystem::exists(path)) {
    return std::nullopt;
  }

  std::ifstream input(path);
  if (!input.is_open()) {
    throw std::runtime_error("failed to open runtime status file: " + path);
  }

  json value;
  input >> value;
  return RuntimeStatusFromJson(value);
}

void SaveRuntimeStatusJson(const RuntimeStatus& status, const std::string& path) {
  const std::filesystem::path file_path(path);
  if (file_path.has_parent_path()) {
    std::filesystem::create_directories(file_path.parent_path());
  }

  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    throw std::runtime_error("failed to open runtime status file for write: " + path);
  }

  output << SerializeRuntimeStatusJson(status) << "\n";
  if (!output.good()) {
    throw std::runtime_error("failed to write runtime status file: " + path);
  }
}

}  // namespace comet
