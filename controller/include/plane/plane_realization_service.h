#pragma once

#include <optional>
#include <string>
#include <vector>

#include "infra/controller_runtime_support_service.h"

#include "comet/execution_plan.h"
#include "comet/models.h"
#include "comet/scheduling_policy.h"
#include "comet/sqlite_store.h"

namespace comet::controller {

class PlaneRealizationService {
 public:
  PlaneRealizationService(
      const ControllerRuntimeSupportService* runtime_support_service,
      int default_stale_after_seconds);

  void MaterializeComposeArtifacts(
      const comet::DesiredState& desired_state,
      const std::vector<comet::NodeExecutionPlan>& host_plans) const;

  void MaterializeInferRuntimeArtifact(
      const comet::DesiredState& desired_state,
      const std::string& artifacts_root) const;

  std::vector<comet::HostAssignment> BuildHostAssignments(
      const comet::DesiredState& desired_state,
      const std::string& artifacts_root,
      int desired_generation,
      const std::vector<comet::NodeAvailabilityOverride>& availability_overrides,
      const std::vector<comet::HostObservation>& observations,
      const std::optional<comet::SchedulingPolicyReport>& scheduling_report) const;

  std::vector<comet::HostAssignment> BuildStopPlaneAssignments(
      const comet::DesiredState& desired_state,
      int desired_generation,
      const std::string& artifacts_root,
      const std::vector<comet::NodeAvailabilityOverride>& availability_overrides) const;

  std::vector<comet::HostAssignment> BuildDeletePlaneAssignments(
      const comet::DesiredState& desired_state,
      int desired_generation,
      const std::string& artifacts_root) const;

  std::optional<comet::HostAssignment> FindLatestHostAssignmentForNode(
      const std::vector<comet::HostAssignment>& assignments,
      const std::string& node_name) const;

  std::optional<comet::HostAssignment> FindLatestHostAssignmentForPlane(
      const std::vector<comet::HostAssignment>& assignments,
      const std::string& plane_name) const;

  std::optional<std::string> ObservedSchedulingGateReason(
      const std::vector<comet::HostObservation>& observations,
      const std::string& node_name,
      int stale_after_seconds) const;

 private:
  bool IsNodeSchedulable(comet::NodeAvailability availability) const;

  const ControllerRuntimeSupportService* runtime_support_service_;
  int default_stale_after_seconds_ = 0;
};

}  // namespace comet::controller
