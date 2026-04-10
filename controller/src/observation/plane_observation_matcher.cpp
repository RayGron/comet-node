#include "observation/plane_observation_matcher.h"

#include <algorithm>

#include "comet/state/state_json.h"

namespace comet::controller {

bool PlaneObservationMatcher::MatchesPlaneInstanceName(
    const std::string& instance_name,
    const std::string& plane_name,
    const std::set<std::string>& plane_instance_names) const {
  if (plane_name.empty()) {
    return true;
  }
  if (!plane_instance_names.empty()) {
    return plane_instance_names.find(instance_name) != plane_instance_names.end();
  }

  for (const auto& prefix : RuntimeInstancePrefixes()) {
    const std::string stem = prefix + plane_name;
    if (instance_name == stem || instance_name.rfind(stem + "-", 0) == 0) {
      return true;
    }
  }
  return false;
}

std::set<std::string> PlaneObservationMatcher::CollectPlaneInstanceNames(
    const comet::DesiredState& observed_state,
    const std::string& plane_name) const {
  std::set<std::string> names;
  for (const auto& instance : observed_state.instances) {
    if (instance.plane_name == plane_name) {
      names.insert(instance.name);
    }
  }
  return names;
}

comet::DesiredState PlaneObservationMatcher::FilterObservedStateForPlane(
    const comet::DesiredState& observed_state,
    const std::string& plane_name) const {
  comet::DesiredState filtered = observed_state;
  filtered.plane_name = plane_name;

  filtered.disks.erase(
      std::remove_if(
          filtered.disks.begin(),
          filtered.disks.end(),
          [&](const comet::DiskSpec& disk) { return disk.plane_name != plane_name; }),
      filtered.disks.end());
  filtered.instances.erase(
      std::remove_if(
          filtered.instances.begin(),
          filtered.instances.end(),
          [&](const comet::InstanceSpec& instance) {
            return instance.plane_name != plane_name;
          }),
      filtered.instances.end());

  const auto plane_instance_names = CollectPlaneInstanceNames(filtered, plane_name);
  filtered.worker_group.members.erase(
      std::remove_if(
          filtered.worker_group.members.begin(),
          filtered.worker_group.members.end(),
          [&](const comet::WorkerGroupMemberSpec& member) {
            return !MatchesPlaneInstanceName(
                       member.name, plane_name, plane_instance_names) &&
                   !MatchesPlaneInstanceName(
                       member.infer_instance_name, plane_name, plane_instance_names);
          }),
      filtered.worker_group.members.end());
  filtered.runtime_gpu_nodes.erase(
      std::remove_if(
          filtered.runtime_gpu_nodes.begin(),
          filtered.runtime_gpu_nodes.end(),
          [&](const comet::RuntimeGpuNode& gpu_node) {
            return !MatchesPlaneInstanceName(
                gpu_node.name, plane_name, plane_instance_names);
          }),
      filtered.runtime_gpu_nodes.end());

  std::set<std::string> plane_node_names;
  for (const auto& instance : filtered.instances) {
    if (!instance.node_name.empty()) {
      plane_node_names.insert(instance.node_name);
    }
  }
  for (const auto& disk : filtered.disks) {
    if (!disk.node_name.empty()) {
      plane_node_names.insert(disk.node_name);
    }
  }
  if (!plane_node_names.empty()) {
    filtered.nodes.erase(
        std::remove_if(
            filtered.nodes.begin(),
            filtered.nodes.end(),
            [&](const comet::NodeInventory& node) {
              return plane_node_names.find(node.name) == plane_node_names.end();
            }),
        filtered.nodes.end());
  }

  filtered.worker_group.expected_workers = static_cast<int>(std::count_if(
      filtered.worker_group.members.begin(),
      filtered.worker_group.members.end(),
      [](const comet::WorkerGroupMemberSpec& member) { return member.enabled; }));
  if (!MatchesPlaneInstanceName(
          filtered.worker_group.infer_instance_name, plane_name, plane_instance_names)) {
    const auto infer_it = std::find_if(
        filtered.instances.begin(),
        filtered.instances.end(),
        [](const comet::InstanceSpec& instance) {
          return instance.role == comet::InstanceRole::Infer;
        });
    filtered.worker_group.infer_instance_name =
        infer_it != filtered.instances.end() ? infer_it->name : std::string{};
  }

  const auto shared_disk_it = std::find_if(
      filtered.disks.begin(),
      filtered.disks.end(),
      [](const comet::DiskSpec& disk) { return disk.kind == comet::DiskKind::PlaneShared; });
  filtered.plane_shared_disk_name =
      shared_disk_it != filtered.disks.end() ? shared_disk_it->name : std::string{};

  return filtered;
}

std::optional<comet::DesiredState> PlaneObservationMatcher::ParseObservedStateForPlane(
    const comet::HostObservation& observation,
    const std::optional<std::string>& plane_name) const {
  if (observation.observed_state_json.empty()) {
    return std::nullopt;
  }

  const auto observed_state =
      comet::DeserializeDesiredStateJson(observation.observed_state_json);
  if (!plane_name.has_value()) {
    return observed_state;
  }

  const auto filtered = FilterObservedStateForPlane(observed_state, *plane_name);
  if (observed_state.plane_name == *plane_name || HasPlaneEntities(filtered)) {
    return filtered;
  }
  return std::nullopt;
}

bool PlaneObservationMatcher::ObservationMatchesPlane(
    const comet::HostObservation& observation,
    const std::string& plane_name) const {
  if (observation.plane_name == plane_name) {
    return true;
  }
  const auto observed_state = ParseObservedStateForPlane(observation, plane_name);
  if (observed_state.has_value()) {
    return true;
  }
  if (observation.instance_runtime_json.empty()) {
    return false;
  }

  try {
    for (const auto& status :
         comet::DeserializeRuntimeStatusListJson(observation.instance_runtime_json)) {
      if (MatchesPlaneInstanceName(status.instance_name, plane_name)) {
        return true;
      }
    }
  } catch (const std::exception&) {
  }
  return false;
}

std::vector<comet::HostObservation> PlaneObservationMatcher::FilterHostObservationsForPlane(
    const std::vector<comet::HostObservation>& observations,
    const std::string& plane_name) const {
  std::vector<comet::HostObservation> result;
  for (const auto& observation : observations) {
    if (ObservationMatchesPlane(observation, plane_name)) {
      result.push_back(observation);
    }
  }
  return result;
}

std::optional<comet::RuntimeStatus> PlaneObservationMatcher::FilterRuntimeStatusForPlane(
    const std::optional<comet::RuntimeStatus>& runtime_status,
    const std::optional<std::string>& plane_name,
    const std::set<std::string>& plane_instance_names) const {
  if (!runtime_status.has_value() || !plane_name.has_value()) {
    return runtime_status;
  }
  if (!runtime_status->plane_name.empty() && runtime_status->plane_name != *plane_name) {
    return std::nullopt;
  }
  if (!runtime_status->instance_name.empty() &&
      !MatchesPlaneInstanceName(
          runtime_status->instance_name, *plane_name, plane_instance_names)) {
    return std::nullopt;
  }
  return runtime_status;
}

std::vector<comet::RuntimeProcessStatus>
PlaneObservationMatcher::FilterInstanceRuntimeStatusesForPlane(
    const std::vector<comet::RuntimeProcessStatus>& statuses,
    const std::optional<std::string>& plane_name,
    const std::set<std::string>& plane_instance_names) const {
  if (!plane_name.has_value()) {
    return statuses;
  }

  std::vector<comet::RuntimeProcessStatus> filtered;
  for (const auto& status : statuses) {
    if (MatchesPlaneInstanceName(
            status.instance_name, *plane_name, plane_instance_names)) {
      filtered.push_back(status);
    }
  }
  return filtered;
}

std::optional<comet::GpuTelemetrySnapshot> PlaneObservationMatcher::FilterGpuTelemetryForPlane(
    const std::optional<comet::GpuTelemetrySnapshot>& snapshot,
    const std::optional<std::string>& plane_name,
    const std::set<std::string>& plane_instance_names) const {
  if (!snapshot.has_value() || !plane_name.has_value()) {
    return snapshot;
  }

  comet::GpuTelemetrySnapshot filtered = *snapshot;
  for (auto& device : filtered.devices) {
    device.processes.erase(
        std::remove_if(
            device.processes.begin(),
            device.processes.end(),
            [&](const comet::GpuProcessTelemetry& process) {
              return process.instance_name != "unknown" &&
                     !MatchesPlaneInstanceName(
                         process.instance_name, *plane_name, plane_instance_names);
            }),
        device.processes.end());
  }
  return filtered;
}

std::optional<comet::DiskTelemetrySnapshot> PlaneObservationMatcher::FilterDiskTelemetryForPlane(
    const std::optional<comet::DiskTelemetrySnapshot>& snapshot,
    const std::optional<std::string>& plane_name) const {
  if (!snapshot.has_value() || !plane_name.has_value()) {
    return snapshot;
  }

  comet::DiskTelemetrySnapshot filtered = *snapshot;
  filtered.items.erase(
      std::remove_if(
          filtered.items.begin(),
          filtered.items.end(),
          [&](const comet::DiskTelemetryRecord& item) {
            return item.plane_name != *plane_name;
          }),
      filtered.items.end());
  return filtered;
}

bool PlaneObservationMatcher::HasPlaneEntities(
    const comet::DesiredState& observed_state) const {
  return !observed_state.instances.empty() || !observed_state.disks.empty() ||
         !observed_state.worker_group.members.empty() ||
         !observed_state.runtime_gpu_nodes.empty();
}

const std::vector<std::string>& PlaneObservationMatcher::RuntimeInstancePrefixes() const {
  static const std::vector<std::string> prefixes = {
      "infer-",
      "worker-",
      "skills-",
      "app-",
      "webgateway-",
      "browsing-",
  };
  return prefixes;
}

}  // namespace comet::controller
