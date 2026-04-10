#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "scheduler/scheduler_execution_dependencies.h"
#include "scheduler/scheduler_service.h"

#include "comet/runtime/runtime_status.h"
#include "comet/state/sqlite_store.h"

namespace comet::controller {

struct SchedulerExecutionVerificationConfig {
  int verification_timeout_seconds = 0;
  int verification_stable_samples_required = 0;
};

class SchedulerExecutionSupport {
 public:
  SchedulerExecutionSupport(
      std::shared_ptr<const SchedulerAssignmentQuerySupport> assignment_query_support,
      std::shared_ptr<const SchedulerVerificationSupport> verification_support,
      SchedulerExecutionVerificationConfig verification_config);

  std::optional<comet::RolloutActionRecord> FindPriorRolloutActionForWorker(
      const std::vector<comet::RolloutActionRecord>& actions,
      const comet::RolloutActionRecord& action,
      const std::string& requested_action_name) const;

  void MaterializeRetryPlacementAction(
      comet::DesiredState* state,
      const comet::RolloutActionRecord& action,
      const std::vector<std::string>& victim_worker_names) const;

  std::vector<comet::HostAssignment> BuildEvictionAssignmentsForAction(
      const comet::DesiredState& desired_state,
      int desired_generation,
      const comet::RolloutActionRecord& action,
      const std::vector<comet::HostAssignment>& existing_assignments) const;

  bool AreRolloutEvictionAssignmentsApplied(
      const std::vector<comet::HostAssignment>& assignments,
      int action_id) const;

  void MaterializeRebalancePlanEntry(
      comet::DesiredState* state,
      const RebalancePlanEntry& entry) const;

  SchedulerVerificationResult EvaluateSchedulerActionVerification(
      const comet::SchedulerPlaneRuntime& plane_runtime,
      const std::vector<comet::HostObservation>& observations) const;

  void MarkWorkerMoveVerified(
      comet::ControllerStore* store,
      const comet::SchedulerPlaneRuntime& plane_runtime) const;

  void MarkWorkersEvicted(
      comet::ControllerStore* store,
      const std::string& plane_name,
      const std::vector<std::string>& worker_names) const;

 private:
  void RemoveWorkerFromDesiredState(
      comet::DesiredState* state,
      const std::string& worker_name) const;

  const comet::RuntimeProcessStatus* FindInstanceRuntimeStatus(
      const std::vector<comet::RuntimeProcessStatus>& statuses,
      const std::string& instance_name,
      const std::string& gpu_device) const;

  bool TelemetryShowsOwnedProcess(
      const std::optional<comet::GpuTelemetrySnapshot>& telemetry,
      const std::string& gpu_device,
      const std::string& instance_name) const;

  std::shared_ptr<const SchedulerAssignmentQuerySupport> assignment_query_support_;
  std::shared_ptr<const SchedulerVerificationSupport> verification_support_;
  SchedulerExecutionVerificationConfig verification_config_;
};

}  // namespace comet::controller
