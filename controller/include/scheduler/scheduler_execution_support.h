#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "scheduler/scheduler_service.h"

#include "comet/runtime_status.h"
#include "comet/sqlite_store.h"

namespace comet::controller {

class SchedulerExecutionSupport {
 public:
  using FindLatestHostAssignmentForNodeFn =
      std::function<std::optional<comet::HostAssignment>(
          const std::vector<comet::HostAssignment>&,
          const std::string&)>;
  using FindLatestHostAssignmentForPlaneFn =
      std::function<std::optional<comet::HostAssignment>(
          const std::vector<comet::HostAssignment>&,
          const std::string&)>;
  using DefaultArtifactsRootProvider = std::function<std::string()>;
  using FindHostObservationForNodeFn =
      std::function<std::optional<comet::HostObservation>(
          const std::vector<comet::HostObservation>&,
          const std::string&)>;
  using ParseInstanceRuntimeStatusesFn =
      std::function<std::vector<comet::RuntimeProcessStatus>(
          const comet::HostObservation&)>;
  using ParseGpuTelemetryFn =
      std::function<std::optional<comet::GpuTelemetrySnapshot>(
          const comet::HostObservation&)>;
  using TimestampAgeSecondsFn =
      std::function<std::optional<long long>(const std::string&)>;
  using VerificationTimeoutSecondsFn = std::function<int()>;
  using VerificationStableSamplesRequiredFn = std::function<int()>;
  using UtcNowSqlTimestampFn = std::function<std::string()>;

  struct Deps {
    FindLatestHostAssignmentForNodeFn find_latest_host_assignment_for_node;
    FindLatestHostAssignmentForPlaneFn find_latest_host_assignment_for_plane;
    DefaultArtifactsRootProvider default_artifacts_root_provider;
    FindHostObservationForNodeFn find_host_observation_for_node;
    ParseInstanceRuntimeStatusesFn parse_instance_runtime_statuses;
    ParseGpuTelemetryFn parse_gpu_telemetry;
    TimestampAgeSecondsFn timestamp_age_seconds;
    VerificationTimeoutSecondsFn verification_timeout_seconds;
    VerificationStableSamplesRequiredFn verification_stable_samples_required;
    UtcNowSqlTimestampFn utc_now_sql_timestamp;
  };

  explicit SchedulerExecutionSupport(Deps deps);

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

  Deps deps_;
};

}  // namespace comet::controller
