#include "read_model/state_aggregate_loader.h"

#include <set>
#include <stdexcept>

namespace comet::controller {

StateAggregateLoader::StateAggregateLoader(Deps deps) : deps_(std::move(deps)) {}

RolloutActionsViewData StateAggregateLoader::LoadRolloutActionsViewData(
    const std::string& db_path,
    const std::optional<std::string>& node_name,
    const std::optional<std::string>& plane_name) const {
  if (deps_.scheduler_domain_service == nullptr) {
    throw std::runtime_error("scheduler domain service is not configured");
  }

  comet::ControllerStore store(db_path);
  store.Initialize();

  RolloutActionsViewData view;
  view.db_path = db_path;
  view.plane_name = plane_name;
  view.node_name = node_name;
  view.desired_state =
      plane_name.has_value() ? store.LoadDesiredState(*plane_name)
                             : store.LoadDesiredState();
  view.desired_generation =
      plane_name.has_value() ? store.LoadDesiredGeneration(*plane_name)
                             : store.LoadDesiredGeneration();
  view.actions =
      view.desired_state.has_value()
          ? store.LoadRolloutActions(view.desired_state->plane_name, node_name)
          : store.LoadRolloutActions(plane_name, node_name);

  std::set<std::string> worker_names;
  std::set<std::string> node_names;
  for (const auto& action : view.actions) {
    worker_names.insert(action.worker_name);
    node_names.insert(action.target_node_name);
  }
  view.gated_worker_count = worker_names.size();
  view.gated_node_count = node_names.size();

  if (view.desired_state.has_value()) {
    view.scheduler_runtime =
        deps_.load_scheduler_runtime_view(store, view.desired_state);
    if (view.desired_generation.has_value()) {
      const auto plane_assignments =
          store.LoadHostAssignments(std::nullopt, std::nullopt, view.desired_state->plane_name);
      const auto plane_observations =
          deps_.filter_host_observations_for_plane(
              store.LoadHostObservations(),
              view.desired_state->plane_name);
      view.lifecycle = deps_.scheduler_domain_service->BuildRolloutLifecycleEntries(
          *view.desired_state,
          *view.desired_generation,
          view.actions,
          plane_assignments,
          plane_observations);
    }
  }
  return view;
}

RebalancePlanViewData StateAggregateLoader::LoadRebalancePlanViewData(
    const std::string& db_path,
    const std::optional<std::string>& node_name,
    int stale_after_seconds,
    const std::optional<std::string>& plane_name) const {
  if (deps_.scheduler_domain_service == nullptr ||
      deps_.scheduler_view_service == nullptr) {
    throw std::runtime_error("scheduler services are not configured");
  }

  comet::ControllerStore store(db_path);
  store.Initialize();

  RebalancePlanViewData view;
  view.db_path = db_path;
  view.plane_name = plane_name;
  view.node_name = node_name;
  view.stale_after_seconds = stale_after_seconds;
  view.desired_state =
      plane_name.has_value() ? store.LoadDesiredState(*plane_name)
                             : store.LoadDesiredState();
  if (!view.desired_state.has_value()) {
    return view;
  }

  view.desired_generation =
      (plane_name.has_value() ? store.LoadDesiredGeneration(*plane_name)
                              : store.LoadDesiredGeneration())
          .value_or(0);
  const auto observations =
      deps_.filter_host_observations_for_plane(
          store.LoadHostObservations(),
          view.desired_state->plane_name);
  const auto assignments =
      store.LoadHostAssignments(std::nullopt, std::nullopt, view.desired_state->plane_name);
  const auto availability_overrides = store.LoadNodeAvailabilityOverrides();
  const auto scheduling_report =
      deps_.evaluate_scheduling_policy(*view.desired_state);
  view.scheduler_runtime =
      deps_.load_scheduler_runtime_view(store, view.desired_state);
  const auto rollout_actions = store.LoadRolloutActions(view.desired_state->plane_name);
  const auto rollout_lifecycle =
      deps_.scheduler_domain_service->BuildRolloutLifecycleEntries(
          *view.desired_state,
          view.desired_generation,
          rollout_actions,
          assignments,
          observations);
  view.rebalance_entries =
      deps_.scheduler_domain_service->BuildRebalancePlanEntries(
          *view.desired_state,
          scheduling_report,
          availability_overrides,
          rollout_lifecycle,
          assignments,
          view.scheduler_runtime,
          observations,
          stale_after_seconds,
          node_name);
  view.controller_gate_summary =
      deps_.scheduler_domain_service->BuildRebalanceControllerGateSummary(
          *view.desired_state,
          view.desired_generation,
          availability_overrides,
          rollout_lifecycle,
          assignments,
          view.scheduler_runtime,
          observations,
          stale_after_seconds);
  view.iteration_budget_summary =
      deps_.scheduler_view_service->BuildRebalanceIterationBudgetSummary(
          store.LoadRebalanceIteration().value_or(0),
          deps_.maximum_rebalance_iterations());
  view.policy_summary =
      deps_.scheduler_view_service->BuildRebalancePolicySummary(view.rebalance_entries);
  view.loop_status =
      deps_.scheduler_view_service->BuildRebalanceLoopStatusSummary(
          view.controller_gate_summary,
          view.iteration_budget_summary,
          view.policy_summary);
  return view;
}

StateAggregateViewData StateAggregateLoader::LoadStateAggregateViewData(
    const std::string& db_path,
    int stale_after_seconds,
    const std::optional<std::string>& plane_name) const {
  if (deps_.scheduler_domain_service == nullptr ||
      deps_.scheduler_view_service == nullptr) {
    throw std::runtime_error("scheduler services are not configured");
  }

  comet::ControllerStore store(db_path);
  store.Initialize();

  StateAggregateViewData view;
  view.db_path = db_path;
  view.stale_after_seconds = stale_after_seconds;
  view.planes = store.LoadPlanes();
  view.desired_states = store.LoadDesiredStates();
  view.desired_state =
      plane_name.has_value() ? store.LoadDesiredState(*plane_name)
                             : store.LoadDesiredState();
  view.desired_generation =
      plane_name.has_value() ? store.LoadDesiredGeneration(*plane_name)
                             : store.LoadDesiredGeneration();
  if (!view.desired_state.has_value()) {
    return view;
  }

  view.disk_runtime_states = store.LoadDiskRuntimeStates(view.desired_state->plane_name);
  view.scheduling_report = deps_.evaluate_scheduling_policy(*view.desired_state);
  view.observations =
      plane_name.has_value()
          ? deps_.filter_host_observations_for_plane(
                store.LoadHostObservations(),
                *plane_name)
          : store.LoadHostObservations();
  view.assignments =
      plane_name.has_value()
          ? store.LoadHostAssignments(std::nullopt, std::nullopt, *plane_name)
          : store.LoadHostAssignments();
  view.availability_overrides = store.LoadNodeAvailabilityOverrides();
  view.scheduler_runtime =
      deps_.load_scheduler_runtime_view(store, view.desired_state);
  const auto plane_rollout_actions = store.LoadRolloutActions(view.desired_state->plane_name);
  view.rollout_lifecycle =
      view.desired_generation.has_value()
          ? deps_.scheduler_domain_service->BuildRolloutLifecycleEntries(
                *view.desired_state,
                *view.desired_generation,
                plane_rollout_actions,
                view.assignments,
                view.observations)
          : std::vector<RolloutLifecycleEntry>{};
  view.rebalance_entries =
      deps_.scheduler_domain_service->BuildRebalancePlanEntries(
          *view.desired_state,
          view.scheduling_report,
          view.availability_overrides,
          view.rollout_lifecycle,
          view.assignments,
          view.scheduler_runtime,
          view.observations,
          stale_after_seconds);
  view.controller_gate_summary =
      deps_.scheduler_domain_service->BuildRebalanceControllerGateSummary(
          *view.desired_state,
          view.desired_generation.value_or(0),
          view.availability_overrides,
          view.rollout_lifecycle,
          view.assignments,
          view.scheduler_runtime,
          view.observations,
          stale_after_seconds);
  view.iteration_budget_summary =
      deps_.scheduler_view_service->BuildRebalanceIterationBudgetSummary(
          store.LoadRebalanceIteration().value_or(0),
          deps_.maximum_rebalance_iterations());
  view.rebalance_policy_summary =
      deps_.scheduler_view_service->BuildRebalancePolicySummary(view.rebalance_entries);
  view.loop_status =
      deps_.scheduler_view_service->BuildRebalanceLoopStatusSummary(
          view.controller_gate_summary,
          view.iteration_budget_summary,
          view.rebalance_policy_summary);
  return view;
}

}  // namespace comet::controller
