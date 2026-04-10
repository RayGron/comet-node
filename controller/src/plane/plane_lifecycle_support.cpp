#include "plane/plane_lifecycle_support.h"

#include "app/controller_composition_support.h"

namespace comet::controller {

ControllerPlaneLifecycleSupport::ControllerPlaneLifecycleSupport(
    const DesiredStatePolicyService& desired_state_policy_service,
    const PlaneRealizationService& plane_realization_service,
    std::string default_artifacts_root)
    : desired_state_policy_service_(desired_state_policy_service),
      plane_realization_service_(plane_realization_service),
      default_artifacts_root_(std::move(default_artifacts_root)) {}

void ControllerPlaneLifecycleSupport::PrepareDesiredState(
    comet::ControllerStore& store,
    comet::DesiredState* desired_state) const {
  desired_state_policy_service_.ApplyRegisteredHostExecutionModes(store, desired_state);
  desired_state_policy_service_.ResolveDesiredStateDynamicPlacements(store, desired_state);
  desired_state_policy_service_.ValidateDesiredStateForControllerAdmission(
      store,
      *desired_state);
  desired_state_policy_service_.ValidateDesiredStateExecutionModes(*desired_state);
}

void ControllerPlaneLifecycleSupport::AppendPlaneEvent(
    comet::ControllerStore& store,
    const std::string& event_type,
    const std::string& message,
    const nlohmann::json& payload,
    const std::string& plane_name) const {
  composition_support::AppendControllerEvent(
      store,
      "plane",
      event_type,
      message,
      payload,
      plane_name);
}

bool ControllerPlaneLifecycleSupport::CanFinalizeDeletedPlane(
    comet::ControllerStore& store,
    const std::string& plane_name) const {
  return composition_support::CanFinalizeDeletedPlane(store, plane_name);
}

std::optional<comet::HostAssignment>
ControllerPlaneLifecycleSupport::FindLatestHostAssignmentForPlane(
    const std::vector<comet::HostAssignment>& assignments,
    const std::string& plane_name) const {
  return plane_realization_service_.FindLatestHostAssignmentForPlane(assignments, plane_name);
}

std::vector<comet::HostAssignment> ControllerPlaneLifecycleSupport::BuildStartAssignments(
    const comet::DesiredState& desired_state,
    const std::string& artifacts_root,
    int desired_generation,
    const std::vector<comet::NodeAvailabilityOverride>& availability_overrides,
    const std::vector<comet::HostObservation>& observations,
    const comet::SchedulingPolicyReport& scheduling_report) const {
  return plane_realization_service_.BuildHostAssignments(
      desired_state,
      artifacts_root,
      desired_generation,
      availability_overrides,
      observations,
      scheduling_report);
}

std::vector<comet::HostAssignment> ControllerPlaneLifecycleSupport::BuildStopAssignments(
    const comet::DesiredState& desired_state,
    int desired_generation,
    const std::string& artifacts_root,
    const std::vector<comet::NodeAvailabilityOverride>& availability_overrides) const {
  return plane_realization_service_.BuildStopPlaneAssignments(
      desired_state,
      desired_generation,
      artifacts_root,
      availability_overrides);
}

std::vector<comet::HostAssignment> ControllerPlaneLifecycleSupport::BuildDeleteAssignments(
    const comet::DesiredState& desired_state,
    int desired_generation,
    const std::string& artifacts_root) const {
  return plane_realization_service_.BuildDeletePlaneAssignments(
      desired_state,
      desired_generation,
      artifacts_root);
}

std::string ControllerPlaneLifecycleSupport::DefaultArtifactsRoot() const {
  return default_artifacts_root_;
}

}  // namespace comet::controller
