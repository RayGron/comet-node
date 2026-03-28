#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "infra/controller_action.h"
#include "app/controller_service_interfaces.h"
#include "scheduler/scheduler_view_service.h"
#include "read_model/state_aggregate_loader.h"

#include "comet/planning/execution_plan.h"
#include "comet/state/models.h"
#include "comet/planning/scheduling_policy.h"
#include "comet/state/state_json.h"
#include "comet/state/sqlite_store.h"

namespace comet::controller {

struct SchedulerVerificationResult {
  bool converged = false;
  bool stable = false;
  bool timed_out = false;
  int next_stable_samples = 0;
  std::string detail;
};

class SchedulerService : public ISchedulerService {
 public:
  using EventsQueryAction = std::function<int(
      const std::optional<std::string>&,
      const std::optional<std::string>&,
      const std::optional<std::string>&,
      const std::optional<std::string>&,
      int)>;
  using ActionResultWorker = std::function<ControllerActionResult(const std::string&)>;
  using ActionResultNullary = std::function<ControllerActionResult()>;
  using ActionResultStatus = std::function<ControllerActionResult(
      int,
      const std::string&,
      const std::optional<std::string>&)>;
  using ActionResultId = std::function<ControllerActionResult(int)>;
  using EventAppender = std::function<void(
      comet::ControllerStore&,
      const std::string&,
      const std::string&,
      const std::string&,
      const nlohmann::json&,
      const std::string&,
      const std::string&,
      const std::string&,
      const std::optional<int>&,
      const std::optional<int>&,
      const std::string&)>;
  using FindRolloutActionByIdFn =
      std::function<std::optional<comet::RolloutActionRecord>(
          const std::vector<comet::RolloutActionRecord>&,
          int)>;
  using FindPriorRolloutActionForWorkerFn =
      std::function<std::optional<comet::RolloutActionRecord>(
          const std::vector<comet::RolloutActionRecord>&,
          const comet::RolloutActionRecord&,
          const std::string&)>;
  using BuildEvictionAssignmentsForActionFn =
      std::function<std::vector<comet::HostAssignment>(
          const comet::DesiredState&,
          int,
          const comet::RolloutActionRecord&,
          const std::vector<comet::HostAssignment>&)>;
  using AreRolloutEvictionAssignmentsAppliedFn =
      std::function<bool(const std::vector<comet::HostAssignment>&, int)>;
  using MarkWorkersEvictedFn =
      std::function<void(
          comet::ControllerStore*,
          const std::string&,
          const std::vector<std::string>&)>;
  using MaterializeRetryPlacementActionFn =
      std::function<void(
          comet::DesiredState*,
          const comet::RolloutActionRecord&,
          const std::vector<std::string>&)>;
  using MaterializeRebalancePlanEntryFn =
      std::function<void(comet::DesiredState*, const RebalancePlanEntry&)>;
  using EvaluateSchedulingPolicyFn =
      std::function<comet::SchedulingPolicyReport(const comet::DesiredState&)>;
  using BuildHostAssignmentsFn =
      std::function<std::vector<comet::HostAssignment>(
          const comet::DesiredState&,
          const std::string&,
          int,
          const std::vector<comet::NodeAvailabilityOverride>&,
          const std::vector<comet::HostObservation>&,
          const comet::SchedulingPolicyReport&)>;
  using BuildAvailabilityOverrideMapFn =
      std::function<std::map<std::string, comet::NodeAvailabilityOverride>(
          const std::vector<comet::NodeAvailabilityOverride>&)>;
  using PrintStateSummaryFn =
      std::function<void(const comet::DesiredState&)>;
  using PrintSchedulerDecisionSummaryFn =
      std::function<void(const comet::DesiredState&)>;
  using PrintRolloutGateSummaryFn =
      std::function<void(const comet::SchedulingPolicyReport&)>;
  using PrintAssignmentDispatchSummaryFn =
      std::function<void(
          const comet::DesiredState&,
          const std::map<std::string, comet::NodeAvailabilityOverride>&,
          const std::vector<comet::HostObservation>&,
          int)>;
  using PrintPersistedRolloutActionsFn =
      std::function<void(const std::vector<comet::RolloutActionRecord>&)>;
  using PrintHostAssignmentsFn =
      std::function<void(const std::vector<comet::HostAssignment>&)>;
  using EvaluateSchedulerActionVerificationFn =
      std::function<SchedulerVerificationResult(
          const comet::SchedulerPlaneRuntime&,
          const std::vector<comet::HostObservation>&)>;
  using MarkWorkerMoveVerifiedFn =
      std::function<void(
          comet::ControllerStore*,
          const comet::SchedulerPlaneRuntime&)>;
  using VerificationStableSamplesRequiredFn = std::function<int()>;
  using UtcNowSqlTimestampFn = std::function<std::string()>;
  using MaterializeComposeArtifactsFn =
      std::function<void(
          const comet::DesiredState&,
          const std::vector<comet::NodeExecutionPlan>&)>;
  using MaterializeInferRuntimeArtifactFn =
      std::function<void(const comet::DesiredState&, const std::string&)>;

  struct Deps {
    std::string db_path;
    std::string artifacts_root;
    int default_stale_after_seconds = 300;
    const StateAggregateLoader* state_aggregate_loader = nullptr;
    const SchedulerViewService* scheduler_view_service = nullptr;
    EventAppender event_appender;
    FindRolloutActionByIdFn find_rollout_action_by_id;
    FindPriorRolloutActionForWorkerFn find_prior_rollout_action_for_worker;
    BuildEvictionAssignmentsForActionFn build_eviction_assignments_for_action;
    AreRolloutEvictionAssignmentsAppliedFn are_rollout_eviction_assignments_applied;
    MarkWorkersEvictedFn mark_workers_evicted;
    MaterializeRetryPlacementActionFn materialize_retry_placement_action;
    MaterializeRebalancePlanEntryFn materialize_rebalance_plan_entry;
    EvaluateSchedulingPolicyFn evaluate_scheduling_policy;
    BuildHostAssignmentsFn build_host_assignments;
    BuildAvailabilityOverrideMapFn build_availability_override_map;
    PrintStateSummaryFn print_state_summary;
    PrintSchedulerDecisionSummaryFn print_scheduler_decision_summary;
    PrintRolloutGateSummaryFn print_rollout_gate_summary;
    PrintAssignmentDispatchSummaryFn print_assignment_dispatch_summary;
    PrintPersistedRolloutActionsFn print_persisted_rollout_actions;
    PrintHostAssignmentsFn print_host_assignments;
    EvaluateSchedulerActionVerificationFn evaluate_scheduler_action_verification;
    MarkWorkerMoveVerifiedFn mark_worker_move_verified;
    VerificationStableSamplesRequiredFn verification_stable_samples_required;
    UtcNowSqlTimestampFn utc_now_sql_timestamp;
    MaterializeComposeArtifactsFn materialize_compose_artifacts;
    MaterializeInferRuntimeArtifactFn materialize_infer_runtime_artifact;
  };

  SchedulerService(
      EventsQueryAction show_events_action,
      Deps deps);

  int ShowRolloutActions(
      const std::optional<std::string>& node_name,
      const std::optional<std::string>& plane_name) const override;
  int ShowRebalancePlan(
      const std::optional<std::string>& node_name,
      const std::optional<std::string>& plane_name) const override;
  int ShowEvents(
      const std::optional<std::string>& plane_name,
      const std::optional<std::string>& node_name,
      const std::optional<std::string>& worker_name,
      const std::optional<std::string>& category,
      int limit) const override;
  ControllerActionResult ExecuteApplyRebalanceProposal(
      const std::string& worker_name) const;
  ControllerActionResult ExecuteReconcileRebalanceProposals() const;
  ControllerActionResult ExecuteSchedulerTick() const;
  ControllerActionResult ExecuteSetRolloutActionStatus(
      int action_id,
      const std::string& requested_status,
      const std::optional<std::string>& message) const;
  ControllerActionResult ExecuteEnqueueRolloutEviction(int action_id) const;
  ControllerActionResult ExecuteReconcileRolloutActions() const;
  ControllerActionResult ExecuteApplyReadyRolloutAction(int action_id) const;
  int ApplyRebalanceProposal(const std::string& worker_name) const override;
  int ReconcileRebalanceProposals() const override;
  int SchedulerTick() const override;
  int SetRolloutActionStatus(
      int action_id,
      const std::string& requested_status,
      const std::optional<std::string>& message) const override;
  int EnqueueRolloutEviction(int action_id) const override;
  int ReconcileRolloutActions() const override;
  int ApplyReadyRolloutAction(int action_id) const override;

 private:
  int AdvanceActiveSchedulerAction() const;
  int SetRolloutActionStatus(
      int action_id,
      comet::RolloutActionStatus status,
      const std::optional<std::string>& status_message) const;

  EventsQueryAction show_events_action_;
  Deps deps_;
};

}  // namespace comet::controller
