#include "scheduler/scheduler_service.h"

#include <algorithm>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace comet::controller {

SchedulerService::SchedulerService(
    EventsQueryAction show_events_action,
    Deps deps)
    : show_events_action_(std::move(show_events_action)),
      deps_(std::move(deps)) {}

int SchedulerService::ShowRolloutActions(
    const std::optional<std::string>& node_name,
    const std::optional<std::string>& plane_name) const {
  if (deps_.state_aggregate_loader == nullptr || deps_.scheduler_view_service == nullptr ||
      !deps_.print_persisted_rollout_actions ||
      !deps_.verification_stable_samples_required) {
    throw std::runtime_error("scheduler aggregate services are not configured");
  }

  const auto view =
      deps_.state_aggregate_loader->LoadRolloutActionsViewData(
          deps_.db_path,
          node_name,
          plane_name);

  std::cout << "db: " << view.db_path << "\n";
  if (view.desired_generation.has_value()) {
    std::cout << "desired generation: " << *view.desired_generation << "\n";
  }
  if (!view.actions.empty()) {
    std::cout << "rollout-gates:\n";
    std::cout << "  gated_workers=" << view.gated_worker_count
              << " gated_nodes=" << view.gated_node_count
              << " deferred_actions=" << view.actions.size() << "\n";
  }
  deps_.print_persisted_rollout_actions(view.actions);
  if (view.scheduler_runtime.has_value()) {
    deps_.scheduler_view_service->PrintSchedulerRuntimeView(
        std::cout,
        *view.scheduler_runtime,
        deps_.verification_stable_samples_required());
  }
  if (!view.lifecycle.empty()) {
    deps_.scheduler_view_service->PrintRolloutLifecycleEntries(
        std::cout,
        view.lifecycle);
  }
  return 0;
}

int SchedulerService::ShowRebalancePlan(
    const std::optional<std::string>& node_name,
    const std::optional<std::string>& plane_name) const {
  if (deps_.state_aggregate_loader == nullptr || deps_.scheduler_view_service == nullptr ||
      !deps_.verification_stable_samples_required) {
    throw std::runtime_error("scheduler aggregate services are not configured");
  }

  const auto view = deps_.state_aggregate_loader->LoadRebalancePlanViewData(
      deps_.db_path,
      node_name,
      deps_.default_stale_after_seconds,
      plane_name);
  if (!view.desired_state.has_value()) {
    std::cout << "rebalance-plan:\n  (empty)\n";
    return 0;
  }

  std::cout << "db: " << view.db_path << "\n";
  std::cout << "desired generation: " << view.desired_generation << "\n";
  deps_.scheduler_view_service->PrintRebalanceControllerGateSummary(
      std::cout,
      view.controller_gate_summary);
  deps_.scheduler_view_service->PrintRebalanceIterationBudgetSummary(
      std::cout,
      view.iteration_budget_summary);
  deps_.scheduler_view_service->PrintRebalanceLoopStatusSummary(
      std::cout,
      view.loop_status);
  deps_.scheduler_view_service->PrintRebalancePlanEntries(
      std::cout,
      view.rebalance_entries);
  deps_.scheduler_view_service->PrintRebalancePolicySummary(
      std::cout,
      view.policy_summary);
  deps_.scheduler_view_service->PrintSchedulerRuntimeView(
      std::cout,
      view.scheduler_runtime,
      deps_.verification_stable_samples_required());
  return 0;
}

int SchedulerService::ShowEvents(
    const std::optional<std::string>& plane_name,
    const std::optional<std::string>& node_name,
    const std::optional<std::string>& worker_name,
    const std::optional<std::string>& category,
    int limit) const {
  return show_events_action_(plane_name, node_name, worker_name, category, limit);
}

ControllerActionResult SchedulerService::ExecuteApplyRebalanceProposal(
    const std::string& worker_name) const {
  return RunControllerActionResult(
      "apply-rebalance-proposal",
      [&]() { return ApplyRebalanceProposal(worker_name); });
}

ControllerActionResult SchedulerService::ExecuteReconcileRebalanceProposals() const {
  return RunControllerActionResult(
      "reconcile-rebalance-proposals",
      [&]() { return ReconcileRebalanceProposals(); });
}

ControllerActionResult SchedulerService::ExecuteSchedulerTick() const {
  return RunControllerActionResult(
      "scheduler-tick",
      [&]() { return SchedulerTick(); });
}

ControllerActionResult SchedulerService::ExecuteSetRolloutActionStatus(
    int action_id,
    const std::string& requested_status,
    const std::optional<std::string>& message) const {
  return RunControllerActionResult(
      "set-rollout-action-status",
      [&]() {
        return SetRolloutActionStatus(
            action_id,
            comet::ParseRolloutActionStatus(requested_status),
            message);
      });
}

ControllerActionResult SchedulerService::ExecuteEnqueueRolloutEviction(int action_id) const {
  return RunControllerActionResult(
      "enqueue-rollout-eviction",
      [&]() { return EnqueueRolloutEviction(action_id); });
}

ControllerActionResult SchedulerService::ExecuteReconcileRolloutActions() const {
  return RunControllerActionResult(
      "reconcile-rollout-actions",
      [&]() { return ReconcileRolloutActions(); });
}

ControllerActionResult SchedulerService::ExecuteApplyReadyRolloutAction(int action_id) const {
  return RunControllerActionResult(
      "apply-ready-rollout-action",
      [&]() { return ApplyReadyRolloutAction(action_id); });
}

int SchedulerService::ApplyRebalanceProposal(const std::string& worker_name) const {
  if (deps_.state_aggregate_loader == nullptr || deps_.scheduler_view_service == nullptr) {
    throw std::runtime_error("scheduler aggregate services are not configured");
  }

  comet::ControllerStore store(deps_.db_path);
  store.Initialize();

  const auto desired_state = store.LoadDesiredState();
  const auto desired_generation = store.LoadDesiredGeneration();
  const auto rebalance_iteration = store.LoadRebalanceIteration();
  if (!desired_state.has_value() || !desired_generation.has_value()) {
    throw std::runtime_error("no desired state found in controller db");
  }

  const auto observations = store.LoadHostObservations();
  const auto rebalance_view = deps_.state_aggregate_loader->LoadRebalancePlanViewData(
      deps_.db_path,
      std::nullopt,
      deps_.default_stale_after_seconds,
      desired_state->plane_name);
  const auto rebalance_entries = rebalance_view.rebalance_entries;

  const auto rebalance_it = std::find_if(
      rebalance_entries.begin(),
      rebalance_entries.end(),
      [&](const RebalancePlanEntry& entry) { return entry.worker_name == worker_name; });
  if (rebalance_it == rebalance_entries.end()) {
    throw std::runtime_error(
        "no rebalance plan entry found for worker '" + worker_name + "'");
  }
  if (rebalance_it->decision != "propose") {
    throw std::runtime_error(
        "worker '" + worker_name + "' is not actionable for rebalance; current decision=" +
        rebalance_it->decision + " state=" + rebalance_it->state);
  }
  const auto iteration_budget_summary = rebalance_view.iteration_budget_summary;
  if (iteration_budget_summary.exhausted) {
    throw std::runtime_error(
        "rebalance iteration budget exhausted (" +
        std::to_string(iteration_budget_summary.current_iteration) + "/" +
        std::to_string(iteration_budget_summary.max_iterations) +
        "); apply a fresh bundle or rollout generation before materializing another direct rebalance");
  }

  comet::DesiredState updated_state = *desired_state;
  deps_.materialize_rebalance_plan_entry(&updated_state, *rebalance_it);
  comet::RequireSchedulingPolicy(updated_state);
  const comet::SchedulingPolicyReport updated_scheduling_report =
      deps_.evaluate_scheduling_policy(updated_state);
  const int next_generation = *desired_generation + 1;
  const auto availability_overrides = store.LoadNodeAvailabilityOverrides();
  const auto host_plans =
      comet::BuildNodeExecutionPlans(desired_state, updated_state, deps_.artifacts_root);

  deps_.materialize_compose_artifacts(updated_state, host_plans);
  deps_.materialize_infer_runtime_artifact(updated_state, deps_.artifacts_root);
  store.ReplaceDesiredState(
      updated_state,
      next_generation,
      rebalance_iteration.value_or(0) + 1);
  store.ReplaceRolloutActions(
      updated_state.plane_name,
      next_generation,
      updated_scheduling_report.rollout_actions);
  store.ReplaceHostAssignments(
      deps_.build_host_assignments(
          updated_state,
          deps_.artifacts_root,
          next_generation,
          availability_overrides,
          observations,
          updated_scheduling_report));
  comet::SchedulerPlaneRuntime plane_runtime;
  plane_runtime.plane_name = updated_state.plane_name;
  plane_runtime.active_action = "rebalance";
  plane_runtime.active_worker_name = rebalance_it->worker_name;
  plane_runtime.phase = "verifying-move";
  plane_runtime.action_generation = next_generation;
  plane_runtime.stable_samples = 0;
  plane_runtime.rollback_attempt_count = 0;
  plane_runtime.source_node_name = rebalance_it->current_node_name;
  plane_runtime.source_gpu_device = rebalance_it->current_gpu_device;
  plane_runtime.target_node_name = rebalance_it->target_node_name;
  plane_runtime.target_gpu_device = rebalance_it->target_gpu_device;
  plane_runtime.previous_state_json = comet::SerializeDesiredStateJson(*desired_state);
  plane_runtime.status_message = "awaiting post-move verification";
  store.UpsertSchedulerPlaneRuntime(plane_runtime);
  deps_.event_appender(
      store,
      "scheduler",
      "rebalance-materialized",
      "materialized safe-direct rebalance proposal",
      nlohmann::json{
          {"desired_generation", next_generation},
          {"source_node", rebalance_it->current_node_name},
          {"source_gpu", rebalance_it->current_gpu_device},
          {"target_node", rebalance_it->target_node_name},
          {"target_gpu", rebalance_it->target_gpu_device},
          {"action", rebalance_it->action},
          {"score", rebalance_it->score},
      },
      updated_state.plane_name,
      rebalance_it->target_node_name,
      rebalance_it->worker_name,
      std::nullopt,
      std::nullopt,
      "info");

  std::cout << "applied rebalance proposal for worker '" << worker_name << "'\n";
  std::cout << "desired generation: " << next_generation << "\n";
  std::cout << "target=" << rebalance_it->target_node_name << ":"
            << rebalance_it->target_gpu_device << "\n";
  deps_.print_state_summary(updated_state);
  std::cout << comet::RenderSchedulingPolicyReport(updated_scheduling_report);
  deps_.print_scheduler_decision_summary(updated_state);
  deps_.print_rollout_gate_summary(updated_scheduling_report);
  deps_.print_assignment_dispatch_summary(
      updated_state,
      deps_.build_availability_override_map(availability_overrides),
      observations,
      deps_.default_stale_after_seconds);
  return 0;
}

int SchedulerService::ReconcileRebalanceProposals() const {
  if (deps_.state_aggregate_loader == nullptr || deps_.scheduler_view_service == nullptr) {
    throw std::runtime_error("scheduler aggregate services are not configured");
  }

  comet::ControllerStore store(deps_.db_path);
  store.Initialize();

  const auto desired_state = store.LoadDesiredState();
  const auto desired_generation = store.LoadDesiredGeneration();
  if (!desired_state.has_value() || !desired_generation.has_value()) {
    throw std::runtime_error("no desired state found in controller db");
  }

  auto rebalance_view = deps_.state_aggregate_loader->LoadRebalancePlanViewData(
      deps_.db_path,
      std::nullopt,
      deps_.default_stale_after_seconds,
      desired_state->plane_name);
  auto rebalance_entries = rebalance_view.rebalance_entries;
  const auto& controller_gate_summary = rebalance_view.controller_gate_summary;
  const auto& iteration_budget_summary = rebalance_view.iteration_budget_summary;
  const auto& rebalance_policy_summary = rebalance_view.policy_summary;
  deps_.scheduler_view_service->PrintRebalanceControllerGateSummary(
      std::cout,
      controller_gate_summary);
  deps_.scheduler_view_service->PrintRebalanceIterationBudgetSummary(
      std::cout,
      iteration_budget_summary);
  deps_.scheduler_view_service->PrintRebalanceLoopStatusSummary(
      std::cout,
      deps_.scheduler_view_service->BuildRebalanceLoopStatusSummary(
          controller_gate_summary,
          iteration_budget_summary,
          rebalance_policy_summary));

  if (!controller_gate_summary.cluster_ready) {
    std::cout << "rebalance proposals: blocked by controller gate\n";
    return 0;
  }

  rebalance_entries.erase(
      std::remove_if(
          rebalance_entries.begin(),
          rebalance_entries.end(),
          [](const RebalancePlanEntry& entry) {
            return entry.decision != "propose" || entry.rebalance_class != "safe-direct";
          }),
      rebalance_entries.end());

  if (rebalance_entries.empty()) {
    std::cout << "rebalance proposals: none actionable\n";
    return 0;
  }
  if (iteration_budget_summary.exhausted) {
    std::cout << "rebalance proposals: blocked by iteration budget\n";
    return 0;
  }

  std::sort(
      rebalance_entries.begin(),
      rebalance_entries.end(),
      [](const RebalancePlanEntry& left, const RebalancePlanEntry& right) {
        if (left.score != right.score) {
          return left.score > right.score;
        }
        return left.worker_name < right.worker_name;
      });

  std::cout << "selected rebalance proposal: worker=" << rebalance_entries.front().worker_name
            << " target=" << rebalance_entries.front().target_node_name << ":"
            << rebalance_entries.front().target_gpu_device
            << " score=" << rebalance_entries.front().score << "\n";
  return ApplyRebalanceProposal(rebalance_entries.front().worker_name);
}

int SchedulerService::SchedulerTick() const {
  comet::ControllerStore store(deps_.db_path);
  store.Initialize();

  const auto desired_state = store.LoadDesiredState();
  const auto desired_generation = store.LoadDesiredGeneration();
  if (!desired_state.has_value() || !desired_generation.has_value()) {
    std::cout << "scheduler-tick: no desired state\n";
    return 0;
  }

  if (const auto plane_runtime = store.LoadSchedulerPlaneRuntime(desired_state->plane_name);
      plane_runtime.has_value() && !plane_runtime->active_action.empty()) {
    std::cout << "scheduler-tick: step=active-scheduler-action\n";
    return AdvanceActiveSchedulerAction();
  }

  const auto rollout_actions = store.LoadRolloutActions(desired_state->plane_name);
  bool has_active_rollout = false;
  for (const auto& action : rollout_actions) {
    if (action.desired_generation == *desired_generation &&
        action.status != comet::RolloutActionStatus::ReadyToRetry) {
      has_active_rollout = true;
      break;
    }
  }
  if (!rollout_actions.empty()) {
    std::cout << "scheduler-tick: step=rollout-reconcile\n";
    return ReconcileRolloutActions();
  }

  std::cout << "scheduler-tick: step=rebalance-reconcile\n";
  if (has_active_rollout) {
    std::cout << "scheduler-tick: rollout still active\n";
    return 0;
  }
  return ReconcileRebalanceProposals();
}

int SchedulerService::SetRolloutActionStatus(
    int action_id,
    comet::RolloutActionStatus status,
    const std::optional<std::string>& status_message) const {
  comet::ControllerStore store(deps_.db_path);
  store.Initialize();
  if (!store.UpdateRolloutActionStatus(action_id, status, status_message.value_or(""))) {
    throw std::runtime_error(
        "rollout action id=" + std::to_string(action_id) + " not found");
  }
  const auto updated_action =
      deps_.find_rollout_action_by_id(store.LoadRolloutActions(), action_id);
  if (updated_action.has_value()) {
    deps_.event_appender(
        store,
        "rollout-action",
        "status-updated",
        "updated rollout action status",
        nlohmann::json{
            {"status", comet::ToString(status)},
            {"status_message", status_message.value_or("")},
            {"action", updated_action->action},
            {"step", updated_action->step},
        },
        updated_action->plane_name,
        updated_action->target_node_name,
        updated_action->worker_name,
        std::nullopt,
        action_id,
        "info");
  }
  std::cout << "updated rollout action id=" << action_id
            << " status=" << comet::ToString(status) << "\n";
  if (updated_action.has_value()) {
    deps_.print_persisted_rollout_actions(
        store.LoadRolloutActions(updated_action->plane_name));
  } else {
    deps_.print_persisted_rollout_actions(store.LoadRolloutActions());
  }
  return 0;
}

int SchedulerService::EnqueueRolloutEviction(int action_id) const {
  comet::ControllerStore store(deps_.db_path);
  store.Initialize();

  const auto desired_state = store.LoadDesiredState();
  const auto desired_generation = store.LoadDesiredGeneration();
  if (!desired_state.has_value() || !desired_generation.has_value()) {
    throw std::runtime_error("no desired state found in controller db");
  }

  const auto rollout_actions = store.LoadRolloutActions(desired_state->plane_name);
  const auto action = deps_.find_rollout_action_by_id(rollout_actions, action_id);
  if (!action.has_value()) {
    throw std::runtime_error(
        "rollout action id=" + std::to_string(action_id) + " not found");
  }
  if (action->desired_generation != *desired_generation) {
    throw std::runtime_error(
        "rollout action id=" + std::to_string(action_id) +
        " does not belong to current desired generation " +
        std::to_string(*desired_generation));
  }
  if (action->action != "evict-best-effort") {
    throw std::runtime_error(
        "rollout action id=" + std::to_string(action_id) +
        " is not an evict-best-effort action");
  }
  if (action->status != comet::RolloutActionStatus::Pending &&
      action->status != comet::RolloutActionStatus::Acknowledged) {
    throw std::runtime_error(
        "rollout action id=" + std::to_string(action_id) +
        " cannot enqueue eviction from status=" +
        comet::ToString(action->status));
  }

  const auto existing_assignments = store.LoadHostAssignments();
  const auto eviction_assignments = deps_.build_eviction_assignments_for_action(
      *desired_state,
      *desired_generation,
      *action,
      existing_assignments);
  store.EnqueueHostAssignments(
      eviction_assignments,
      "superseded by rollout eviction action id=" + std::to_string(action_id));

  std::set<std::string> node_names;
  for (const auto& assignment : eviction_assignments) {
    node_names.insert(assignment.node_name);
  }
  std::ostringstream message;
  message << "eviction assignments enqueued on nodes ";
  bool first = true;
  for (const auto& node_name : node_names) {
    if (!first) {
      message << ",";
    }
    first = false;
    message << node_name;
  }
  store.UpdateRolloutActionStatus(
      action_id,
      comet::RolloutActionStatus::Acknowledged,
      message.str());
  deps_.event_appender(
      store,
      "rollout-action",
      "eviction-enqueued",
      message.str(),
      nlohmann::json{
          {"victims", action->victim_worker_names},
          {"target_node", action->target_node_name},
          {"target_gpu", action->target_gpu_device},
      },
      desired_state->plane_name,
      action->target_node_name,
      action->worker_name,
      std::nullopt,
      action_id,
      "info");

  std::cout << "enqueued rollout eviction action id=" << action_id << "\n";
  deps_.print_persisted_rollout_actions(store.LoadRolloutActions(desired_state->plane_name));
  for (const auto& node_name : node_names) {
    deps_.print_host_assignments(store.LoadHostAssignments(node_name));
  }
  return 0;
}

int SchedulerService::ReconcileRolloutActions() const {
  if (deps_.state_aggregate_loader == nullptr || deps_.scheduler_view_service == nullptr) {
    throw std::runtime_error("scheduler aggregate services are not configured");
  }

  comet::ControllerStore store(deps_.db_path);
  store.Initialize();

  const auto desired_state = store.LoadDesiredState();
  const auto desired_generation = store.LoadDesiredGeneration();
  if (!desired_state.has_value() || !desired_generation.has_value()) {
    throw std::runtime_error("no desired state found in controller db");
  }

  const auto all_rollout_actions = store.LoadRolloutActions(desired_state->plane_name);
  std::vector<comet::RolloutActionRecord> rollout_actions;
  for (const auto& action : all_rollout_actions) {
    if (action.desired_generation == *desired_generation) {
      rollout_actions.push_back(action);
    }
  }

  std::cout << "db: " << deps_.db_path << "\n";
  std::cout << "desired generation: " << *desired_generation << "\n";
  if (rollout_actions.empty()) {
    std::cout << "rollout reconcile: no rollout actions for current generation\n";
    return 0;
  }

  bool changed = false;
  for (const auto& action : rollout_actions) {
    if (action.action == "evict-best-effort") {
      if (action.status == comet::RolloutActionStatus::Pending) {
        const auto existing_assignments = store.LoadHostAssignments();
        const auto eviction_assignments = deps_.build_eviction_assignments_for_action(
            *desired_state,
            *desired_generation,
            action,
            existing_assignments);
        store.EnqueueHostAssignments(
            eviction_assignments,
            "superseded by rollout eviction action id=" + std::to_string(action.id));
        store.UpdateRolloutActionStatus(
            action.id,
            comet::RolloutActionStatus::Acknowledged,
            "controller-managed eviction assignments enqueued");
        std::cout << "rollout reconcile: enqueued eviction action id=" << action.id << "\n";
        changed = true;
        continue;
      }

      if (action.status == comet::RolloutActionStatus::Acknowledged &&
          deps_.are_rollout_eviction_assignments_applied(
              store.LoadHostAssignments(),
              action.id)) {
        store.UpdateRolloutActionStatus(
            action.id,
            comet::RolloutActionStatus::ReadyToRetry,
            "eviction assignments applied");
        deps_.mark_workers_evicted(
            &store,
            desired_state->plane_name,
            action.victim_worker_names);
        std::cout << "rollout reconcile: eviction action id=" << action.id
                  << " is ready-to-retry\n";
        changed = true;
      }
      continue;
    }

    if (action.action != "retry-placement") {
      continue;
    }

    auto current_action = deps_.find_rollout_action_by_id(
        store.LoadRolloutActions(desired_state->plane_name),
        action.id);
    if (!current_action.has_value()) {
      continue;
    }

    const auto prior_evict_action = deps_.find_prior_rollout_action_for_worker(
        store.LoadRolloutActions(desired_state->plane_name),
        *current_action,
        "evict-best-effort");
    if (current_action->status == comet::RolloutActionStatus::Pending &&
        prior_evict_action.has_value() &&
        prior_evict_action->status == comet::RolloutActionStatus::ReadyToRetry) {
      store.UpdateRolloutActionStatus(
          current_action->id,
          comet::RolloutActionStatus::ReadyToRetry,
          "preceding eviction completed");
      std::cout << "rollout reconcile: retry action id=" << current_action->id
                << " is ready-to-retry\n";
      changed = true;
      current_action = deps_.find_rollout_action_by_id(
          store.LoadRolloutActions(desired_state->plane_name),
          action.id);
    }

    if (current_action.has_value() &&
        current_action->status == comet::RolloutActionStatus::ReadyToRetry) {
      std::cout << "rollout reconcile: materializing retry action id="
                << current_action->id << "\n";
      return ApplyReadyRolloutAction(current_action->id);
    }
  }

  if (!changed) {
    std::cout << "rollout reconcile: no state changes\n";
  }
  deps_.print_persisted_rollout_actions(store.LoadRolloutActions(desired_state->plane_name));
  if (const auto state = store.LoadDesiredState(); state.has_value()) {
    if (store.LoadDesiredGeneration().has_value()) {
      const auto rollout_view = deps_.state_aggregate_loader->LoadRolloutActionsViewData(
          deps_.db_path,
          std::nullopt,
          state->plane_name);
      deps_.scheduler_view_service->PrintRolloutLifecycleEntries(
          std::cout,
          rollout_view.lifecycle);
    }
  }
  return 0;
}

int SchedulerService::ApplyReadyRolloutAction(int action_id) const {
  comet::ControllerStore store(deps_.db_path);
  store.Initialize();

  const auto desired_state = store.LoadDesiredState();
  const auto desired_generation = store.LoadDesiredGeneration();
  if (!desired_state.has_value() || !desired_generation.has_value()) {
    throw std::runtime_error("no desired state found in controller db");
  }

  const auto rollout_actions = store.LoadRolloutActions(desired_state->plane_name);
  const auto action = deps_.find_rollout_action_by_id(rollout_actions, action_id);
  if (!action.has_value()) {
    throw std::runtime_error(
        "rollout action id=" + std::to_string(action_id) + " not found");
  }
  if (action->status != comet::RolloutActionStatus::ReadyToRetry) {
    throw std::runtime_error(
        "rollout action id=" + std::to_string(action_id) +
        " is not ready-to-retry; current status=" +
        comet::ToString(action->status));
  }
  if (action->action != "retry-placement") {
    throw std::runtime_error(
        "rollout action id=" + std::to_string(action_id) +
        " is not a retry-placement action");
  }

  std::vector<std::string> victim_worker_names;
  for (const auto& candidate_action : rollout_actions) {
    if (candidate_action.desired_generation != action->desired_generation ||
        candidate_action.worker_name != action->worker_name ||
        candidate_action.step >= action->step) {
      continue;
    }
    if (candidate_action.status != comet::RolloutActionStatus::ReadyToRetry) {
      throw std::runtime_error(
          "prior rollout step id=" + std::to_string(candidate_action.id) +
          " is not ready-to-retry");
    }
    if (candidate_action.action == "evict-best-effort") {
      victim_worker_names = candidate_action.victim_worker_names;
    }
  }

  comet::DesiredState updated_state = *desired_state;
  deps_.materialize_retry_placement_action(&updated_state, *action, victim_worker_names);
  comet::RequireSchedulingPolicy(updated_state);
  const comet::SchedulingPolicyReport scheduling_report =
      deps_.evaluate_scheduling_policy(updated_state);
  const int next_generation = *desired_generation + 1;
  const auto availability_overrides = store.LoadNodeAvailabilityOverrides();
  const auto observations = store.LoadHostObservations();

  store.ReplaceDesiredState(updated_state, next_generation, 0);
  store.ClearSchedulerPlaneRuntime(updated_state.plane_name);
  store.ReplaceRolloutActions(
      updated_state.plane_name, next_generation, scheduling_report.rollout_actions);
  store.ReplaceHostAssignments(
      deps_.build_host_assignments(
          updated_state,
          deps_.artifacts_root,
          next_generation,
          availability_overrides,
          observations,
          scheduling_report));
  deps_.event_appender(
      store,
      "rollout-action",
      "retry-placement-applied",
      "materialized ready rollout action",
      nlohmann::json{
          {"desired_generation", next_generation},
          {"target_node", action->target_node_name},
          {"target_gpu", action->target_gpu_device},
          {"victims", victim_worker_names},
      },
      updated_state.plane_name,
      action->target_node_name,
      action->worker_name,
      std::nullopt,
      action_id,
      "info");

  std::cout << "applied ready rollout action id=" << action_id << "\n";
  std::cout << "desired generation: " << next_generation << "\n";
  deps_.print_state_summary(updated_state);
  std::cout << comet::RenderSchedulingPolicyReport(scheduling_report);
  deps_.print_scheduler_decision_summary(updated_state);
  deps_.print_rollout_gate_summary(scheduling_report);
  return 0;
}

int SchedulerService::AdvanceActiveSchedulerAction() const {
  comet::ControllerStore store(deps_.db_path);
  store.Initialize();

  const auto desired_state = store.LoadDesiredState();
  const auto desired_generation = store.LoadDesiredGeneration();
  if (!desired_state.has_value() || !desired_generation.has_value()) {
    std::cout << "scheduler active-action: no desired state\n";
    return 0;
  }

  const auto plane_runtime = store.LoadSchedulerPlaneRuntime(desired_state->plane_name);
  if (!plane_runtime.has_value() || plane_runtime->active_action.empty()) {
    std::cout << "scheduler active-action: none\n";
    return 0;
  }

  if (plane_runtime->phase == "rollback-planned") {
    if (plane_runtime->previous_state_json.empty()) {
      throw std::runtime_error(
          "rollback-planned action has no previous desired state payload");
    }
    const comet::DesiredState rollback_state =
        comet::DeserializeDesiredStateJson(plane_runtime->previous_state_json);
    comet::RequireSchedulingPolicy(rollback_state);
    const auto availability_overrides = store.LoadNodeAvailabilityOverrides();
    const auto observations = store.LoadHostObservations();
    const auto rollback_report = deps_.evaluate_scheduling_policy(rollback_state);
    const int rollback_generation = *desired_generation + 1;
    store.ReplaceDesiredState(rollback_state, rollback_generation, 0);
    store.ReplaceRolloutActions(
        rollback_state.plane_name,
        rollback_generation,
        rollback_report.rollout_actions);
    store.ReplaceHostAssignments(
        deps_.build_host_assignments(
            rollback_state,
            deps_.artifacts_root,
            rollback_generation,
            availability_overrides,
            observations,
            rollback_report));
    comet::SchedulerPlaneRuntime updated_runtime = *plane_runtime;
    updated_runtime.phase = "rollback-applied";
    updated_runtime.action_generation = rollback_generation;
    updated_runtime.stable_samples = 0;
    updated_runtime.rollback_attempt_count = 1;
    updated_runtime.started_at = deps_.utc_now_sql_timestamp();
    updated_runtime.status_message = "rollback materialized after verification timeout";
    store.UpsertSchedulerPlaneRuntime(updated_runtime);
    deps_.event_appender(
        store,
        "scheduler",
        "rollback-applied",
        updated_runtime.status_message,
        nlohmann::json{
            {"worker", updated_runtime.active_worker_name},
            {"generation", rollback_generation},
            {"phase", updated_runtime.phase},
        },
        rollback_state.plane_name,
        updated_runtime.target_node_name,
        updated_runtime.active_worker_name,
        std::nullopt,
        std::nullopt,
        "info");
    std::cout << "scheduler active-action: rollback-applied worker="
              << updated_runtime.active_worker_name
              << " generation=" << rollback_generation << "\n";
    return 0;
  }

  const auto observations = store.LoadHostObservations();
  const auto verification =
      deps_.evaluate_scheduler_action_verification(*plane_runtime, observations);
  comet::SchedulerPlaneRuntime updated_runtime = *plane_runtime;
  updated_runtime.stable_samples = verification.next_stable_samples;
  updated_runtime.status_message = verification.detail;

  if (verification.stable) {
    deps_.mark_worker_move_verified(&store, updated_runtime);
    deps_.event_appender(
        store,
        "scheduler",
        "move-verified",
        verification.detail,
        nlohmann::json{
            {"worker", updated_runtime.active_worker_name},
            {"generation", updated_runtime.action_generation},
            {"phase", updated_runtime.phase},
            {"stable_samples", updated_runtime.stable_samples},
        },
        updated_runtime.plane_name,
        updated_runtime.target_node_name,
        updated_runtime.active_worker_name,
        std::nullopt,
        std::nullopt,
        "info");
    store.ClearSchedulerPlaneRuntime(updated_runtime.plane_name);
    std::cout << "scheduler active-action: verified worker="
              << updated_runtime.active_worker_name
              << " phase=" << updated_runtime.phase << "\n";
    return 0;
  }

  if (!verification.timed_out) {
    store.UpsertSchedulerPlaneRuntime(updated_runtime);
    std::cout << "scheduler active-action: waiting worker="
              << updated_runtime.active_worker_name
              << " phase=" << updated_runtime.phase
              << " stable_samples=" << updated_runtime.stable_samples << "/"
              << deps_.verification_stable_samples_required()
              << " detail=" << verification.detail << "\n";
    return 0;
  }

  if (updated_runtime.rollback_attempt_count == 0 &&
      !updated_runtime.previous_state_json.empty()) {
    comet::SchedulerWorkerRuntime worker_runtime;
    if (const auto current = store.LoadSchedulerWorkerRuntime(updated_runtime.active_worker_name);
        current.has_value()) {
      worker_runtime = *current;
    }
    worker_runtime.plane_name = updated_runtime.plane_name;
    worker_runtime.worker_name = updated_runtime.active_worker_name;
    worker_runtime.last_scheduler_phase = "failed-verification";
    worker_runtime.last_status_message = verification.detail;
    worker_runtime.manual_intervention_required = false;
    store.UpsertSchedulerWorkerRuntime(worker_runtime);
    updated_runtime.phase = "rollback-planned";
    updated_runtime.stable_samples = 0;
    updated_runtime.started_at = deps_.utc_now_sql_timestamp();
    updated_runtime.status_message = "verification timed out; rollback planned";
    store.UpsertSchedulerPlaneRuntime(updated_runtime);
    deps_.event_appender(
        store,
        "scheduler",
        "rollback-planned",
        verification.detail,
        nlohmann::json{
            {"worker", updated_runtime.active_worker_name},
            {"generation", updated_runtime.action_generation},
            {"phase", updated_runtime.phase},
        },
        updated_runtime.plane_name,
        updated_runtime.target_node_name,
        updated_runtime.active_worker_name,
        std::nullopt,
        std::nullopt,
        "warning");
    std::cout << "scheduler active-action: rollback-planned worker="
              << updated_runtime.active_worker_name
              << " generation=" << updated_runtime.action_generation << "\n";
    return 0;
  }

  comet::SchedulerWorkerRuntime worker_runtime;
  if (const auto current = store.LoadSchedulerWorkerRuntime(updated_runtime.active_worker_name);
      current.has_value()) {
    worker_runtime = *current;
  }
  worker_runtime.plane_name = updated_runtime.plane_name;
  worker_runtime.worker_name = updated_runtime.active_worker_name;
  worker_runtime.last_scheduler_phase = "manual-intervention-required";
  worker_runtime.last_status_message = verification.detail;
  worker_runtime.manual_intervention_required = true;
  store.UpsertSchedulerWorkerRuntime(worker_runtime);
  deps_.event_appender(
      store,
      "scheduler",
      "manual-intervention-required",
      verification.detail,
      nlohmann::json{
          {"worker", updated_runtime.active_worker_name},
          {"generation", updated_runtime.action_generation},
          {"phase", updated_runtime.phase},
      },
      updated_runtime.plane_name,
      updated_runtime.target_node_name,
      updated_runtime.active_worker_name,
      std::nullopt,
      std::nullopt,
      "error");
  store.ClearSchedulerPlaneRuntime(updated_runtime.plane_name);
  std::cout << "scheduler active-action: manual-intervention-required worker="
            << updated_runtime.active_worker_name
            << " detail=" << verification.detail << "\n";
  return 0;
}

int SchedulerService::SetRolloutActionStatus(
    int action_id,
    const std::string& requested_status,
    const std::optional<std::string>& message) const {
  return SetRolloutActionStatus(
      action_id,
      comet::ParseRolloutActionStatus(requested_status),
      message);
}

}  // namespace comet::controller
