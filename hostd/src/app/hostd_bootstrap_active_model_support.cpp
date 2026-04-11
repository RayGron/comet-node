#include "app/hostd_bootstrap_active_model_support.h"

#include <filesystem>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace naim::hostd {

namespace fs = std::filesystem;
using nlohmann::json;

HostdBootstrapActiveModelSupport::HostdBootstrapActiveModelSupport(
    const HostdDesiredStatePathSupport& path_support,
    const HostdFileSupport& file_support,
    const HostdBootstrapModelArtifactSupport& artifact_support)
    : path_support_(path_support),
      file_support_(file_support),
      artifact_support_(artifact_support),
      local_state_path_support_(),
      local_state_repository_(local_state_path_support_) {}

std::string HostdBootstrapActiveModelSupport::ActiveModelPathForNode(
    const naim::DesiredState& state,
    const std::string& node_name) const {
  const auto active_model_path =
      path_support_.ControlFilePathForNode(state, node_name, "active-model.json");
  if (!active_model_path.has_value()) {
    throw std::runtime_error(
        "plane '" + state.plane_name + "' is missing infer control path for node '" + node_name +
        "'");
  }
  return *active_model_path;
}

std::string HostdBootstrapActiveModelSupport::BootstrapRuntimeModelPath(
    const naim::DesiredState& state,
    const std::string& target_host_path) const {
  const std::string node_name =
      local_state_repository_.RequireSingleNodeName(state);
  const naim::DiskSpec& shared_disk =
      artifact_support_.RequirePlaneSharedDiskForNode(state, node_name);
  const fs::path target_path(target_host_path);
  const fs::path shared_root(shared_disk.host_path);
  std::string relative = target_path.lexically_relative(shared_root).generic_string();
  if (relative.empty() || relative == ".") {
    relative = target_path.filename().string();
  }
  return (fs::path(shared_disk.container_path) / relative).generic_string();
}

void HostdBootstrapActiveModelSupport::WriteBootstrapActiveModel(
    const naim::DesiredState& state,
    const std::string& node_name,
    const std::string& target_host_path,
    const std::optional<std::string>& runtime_model_path_override) const {
  const auto& bootstrap_model = *state.bootstrap_model;
  const std::string runtime_model_path =
      runtime_model_path_override.has_value()
          ? *runtime_model_path_override
          : BootstrapRuntimeModelPath(state, target_host_path);
  file_support_.WriteTextFile(
      ActiveModelPathForNode(state, node_name),
      json{
          {"version", 1},
          {"plane_name", state.plane_name},
          {"model_id", bootstrap_model.model_id},
          {"source_model_id", bootstrap_model.model_id},
          {"served_model_name",
           bootstrap_model.served_model_name.has_value()
               ? *bootstrap_model.served_model_name
               : bootstrap_model.model_id},
          {"local_model_path", target_host_path},
          {"cached_local_model_path", target_host_path},
          {"cached_runtime_model_path", runtime_model_path},
          {"runtime_model_path", runtime_model_path},
      }
          .dump(2));
}

}  // namespace naim::hostd
