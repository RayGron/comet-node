#include "plane/plane_registry_query_support.h"

namespace comet::controller {

ControllerPlaneRegistryQuerySupport::ControllerPlaneRegistryQuerySupport(
    const ControllerRuntimeSupportService& runtime_support_service)
    : runtime_support_service_(runtime_support_service) {}

std::vector<comet::HostObservation>
ControllerPlaneRegistryQuerySupport::FilterHostObservationsForPlane(
    const std::vector<comet::HostObservation>& observations,
    const std::string& plane_name) const {
  return plane_observation_matcher_.FilterHostObservationsForPlane(observations, plane_name);
}

int ControllerPlaneRegistryQuerySupport::ComputeEffectiveAppliedGeneration(
    const comet::PlaneRecord& plane,
    const std::optional<comet::DesiredState>& desired_state,
    const std::optional<int>& desired_generation,
    const std::vector<comet::HostObservation>& observations) const {
  if (!desired_state.has_value() || !desired_generation.has_value()) {
    return plane.applied_generation;
  }
  if (*desired_generation <= plane.applied_generation) {
    return plane.applied_generation;
  }
  for (const auto& node : desired_state->nodes) {
    const auto observation =
        runtime_support_service_.FindHostObservationForNode(observations, node.name);
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
}

std::map<std::string, comet::HostAssignment>
ControllerPlaneRegistryQuerySupport::BuildLatestAssignmentsByNode(
    const std::vector<comet::HostAssignment>& assignments) const {
  std::map<std::string, comet::HostAssignment> latest_by_node;
  for (const auto& assignment : assignments) {
    auto it = latest_by_node.find(assignment.node_name);
    if (it == latest_by_node.end() || assignment.id >= it->second.id) {
      latest_by_node[assignment.node_name] = assignment;
    }
  }
  return latest_by_node;
}

}  // namespace comet::controller
