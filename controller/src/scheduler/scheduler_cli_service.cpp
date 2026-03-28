#include "scheduler/scheduler_cli_service.h"

#include <iostream>
#include <stdexcept>
#include <utility>

namespace comet::controller {

SchedulerCliService::SchedulerCliService(Deps deps) : deps_(std::move(deps)) {}

int SchedulerCliService::ShowRolloutActions(
    const std::string& db_path,
    const std::optional<std::string>& node_name,
    const std::optional<std::string>& plane_name) const {
  if (deps_.state_aggregate_loader == nullptr || deps_.scheduler_view_service == nullptr ||
      !deps_.print_persisted_rollout_actions ||
      !deps_.verification_stable_samples_required) {
    throw std::runtime_error("scheduler cli dependencies are not configured");
  }

  const auto view =
      deps_.state_aggregate_loader->LoadRolloutActionsViewData(db_path, node_name, plane_name);

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

int SchedulerCliService::ShowRebalancePlan(
    const std::string& db_path,
    const std::optional<std::string>& node_name,
    const std::optional<std::string>& plane_name) const {
  if (deps_.state_aggregate_loader == nullptr || deps_.scheduler_view_service == nullptr) {
    throw std::runtime_error("scheduler cli dependencies are not configured");
  }

  const auto view = deps_.state_aggregate_loader->LoadRebalancePlanViewData(
      db_path,
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
      deps_.verification_stable_samples_required
          ? deps_.verification_stable_samples_required()
          : 0);
  return 0;
}

}  // namespace comet::controller
