#include "read_model/read_model_cli_service.h"

#include <iostream>
#include <stdexcept>
#include <utility>

namespace comet::controller {

namespace {

struct HostAssignmentsViewData {
  std::string db_path;
  std::optional<std::string> node_name;
  std::vector<comet::HostAssignment> assignments;
};

struct HostObservationsViewData {
  std::string db_path;
  std::optional<std::string> plane_name;
  std::optional<std::string> node_name;
  int stale_after_seconds = 0;
  std::vector<comet::HostObservation> observations;
};

struct HostHealthViewData {
  std::string db_path;
  std::optional<std::string> node_name;
  int stale_after_seconds = 0;
  std::optional<comet::DesiredState> desired_state;
  std::vector<comet::HostObservation> observations;
  std::vector<comet::NodeAvailabilityOverride> availability_overrides;
};

struct DiskStateViewData {
  std::string db_path;
  std::optional<std::string> plane_name;
  std::optional<std::string> node_name;
  std::optional<comet::DesiredState> desired_state;
  std::optional<int> desired_generation;
  std::vector<comet::DiskRuntimeState> runtime_states;
  std::vector<comet::HostObservation> observations;
};

struct EventsViewData {
  std::string db_path;
  std::optional<std::string> plane_name;
  std::optional<std::string> node_name;
  std::optional<std::string> worker_name;
  std::optional<std::string> category;
  int limit = 100;
  std::vector<comet::EventRecord> events;
};

}  // namespace

ReadModelCliService::ReadModelCliService(Deps deps) : deps_(std::move(deps)) {}

int ReadModelCliService::ShowHostAssignments(
    const std::string& db_path,
    const std::optional<std::string>& node_name) const {
  comet::ControllerStore store(db_path);
  store.Initialize();
  const HostAssignmentsViewData view{
      db_path,
      node_name,
      store.LoadHostAssignments(node_name),
  };
  std::cout << "db: " << view.db_path << "\n";
  deps_.print_host_assignments(view.assignments);
  return 0;
}

int ReadModelCliService::ShowHostObservations(
    const std::string& db_path,
    const std::optional<std::string>& plane_name,
    const std::optional<std::string>& node_name,
    int stale_after_seconds) const {
  if (!deps_.filter_host_observations_for_plane) {
    throw std::runtime_error("read model cli filter dependency is not configured");
  }
  comet::ControllerStore store(db_path);
  store.Initialize();
  const auto observations = plane_name.has_value()
                                ? deps_.filter_host_observations_for_plane(
                                      store.LoadHostObservations(node_name),
                                      *plane_name)
                                : store.LoadHostObservations(node_name);
  const HostObservationsViewData view{
      db_path,
      plane_name,
      node_name,
      stale_after_seconds,
      observations,
  };
  std::cout << "db: " << view.db_path << "\n";
  if (view.plane_name.has_value()) {
    std::cout << "plane: " << *view.plane_name << "\n";
  }
  std::cout << "stale_after_seconds: " << view.stale_after_seconds << "\n";
  deps_.print_host_observations(view.observations, view.stale_after_seconds);
  return 0;
}

int ReadModelCliService::ShowNodeAvailability(
    const std::string& db_path,
    const std::optional<std::string>& node_name) const {
  comet::ControllerStore store(db_path);
  store.Initialize();

  std::cout << "db: " << db_path << "\n";
  deps_.print_node_availability_overrides(store.LoadNodeAvailabilityOverrides(node_name));
  return 0;
}

int ReadModelCliService::ShowHostHealth(
    const std::string& db_path,
    const std::optional<std::string>& node_name,
    int stale_after_seconds) const {
  comet::ControllerStore store(db_path);
  store.Initialize();
  const HostHealthViewData view{
      db_path,
      node_name,
      stale_after_seconds,
      store.LoadDesiredState(),
      store.LoadHostObservations(node_name),
      store.LoadNodeAvailabilityOverrides(node_name),
  };
  std::cout << "db: " << view.db_path << "\n";
  std::cout << "stale_after_seconds: " << view.stale_after_seconds << "\n";
  deps_.print_host_health(
      view.desired_state,
      view.observations,
      view.availability_overrides,
      view.node_name,
      view.stale_after_seconds);
  return 0;
}

int ReadModelCliService::ShowEvents(
    const std::string& db_path,
    const std::optional<std::string>& plane_name,
    const std::optional<std::string>& node_name,
    const std::optional<std::string>& worker_name,
    const std::optional<std::string>& category,
    int limit) const {
  comet::ControllerStore store(db_path);
  store.Initialize();
  const EventsViewData view{
      db_path,
      plane_name,
      node_name,
      worker_name,
      category,
      limit,
      store.LoadEvents(
          plane_name,
          node_name,
          worker_name,
          category,
          limit),
  };
  std::cout << "db: " << view.db_path << "\n";
  std::cout << "limit: " << view.limit << "\n";
  if (view.plane_name.has_value()) {
    std::cout << "plane: " << *view.plane_name << "\n";
  }
  if (view.node_name.has_value()) {
    std::cout << "node: " << *view.node_name << "\n";
  }
  if (view.worker_name.has_value()) {
    std::cout << "worker: " << *view.worker_name << "\n";
  }
  if (view.category.has_value()) {
    std::cout << "category: " << *view.category << "\n";
  }
  deps_.print_events(view.events);
  return 0;
}

int ReadModelCliService::ShowState(const std::string& db_path) const {
  if (deps_.state_aggregate_loader == nullptr || deps_.scheduler_view_service == nullptr ||
      !deps_.default_stale_after_seconds || !deps_.verification_stable_samples_required) {
    throw std::runtime_error("read model cli state dependencies are not configured");
  }
  const auto view = deps_.state_aggregate_loader->LoadStateAggregateViewData(
      db_path, deps_.default_stale_after_seconds());
  if (!view.desired_state.has_value()) {
    std::cout << "state: empty\n";
    return 0;
  }

  std::cout << "db: " << view.db_path << "\n";
  if (view.desired_generation.has_value()) {
    std::cout << "desired generation: " << *view.desired_generation << "\n";
  }
  deps_.print_state_summary(*view.desired_state);
  deps_.print_disk_runtime_states(view.disk_runtime_states);
  deps_.print_detailed_disk_state(
      *view.desired_state,
      view.disk_runtime_states,
      view.observations,
      std::nullopt);
  std::cout << comet::RenderSchedulingPolicyReport(view.scheduling_report);
  deps_.print_scheduler_decision_summary(*view.desired_state);
  deps_.print_rollout_gate_summary(view.scheduling_report);
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
      view.rebalance_policy_summary);
  deps_.scheduler_view_service->PrintSchedulerRuntimeView(
      std::cout,
      view.scheduler_runtime,
      deps_.verification_stable_samples_required());
  if (view.desired_generation.has_value()) {
    deps_.scheduler_view_service->PrintRolloutLifecycleEntries(
        std::cout,
        view.rollout_lifecycle);
  }
  std::cout << "\n";
  deps_.print_node_availability_overrides(view.availability_overrides);
  std::cout << "\n";
  deps_.print_host_observations(view.observations, view.stale_after_seconds);
  std::cout << "\n";
  deps_.print_host_health(
      view.desired_state,
      view.observations,
      view.availability_overrides,
      std::nullopt,
      view.stale_after_seconds);
  return 0;
}

int ReadModelCliService::ShowDiskState(
    const std::string& db_path,
    const std::optional<std::string>& node_name,
    const std::optional<std::string>& plane_name) const {
  if (!deps_.filter_host_observations_for_plane) {
    throw std::runtime_error("read model cli filter dependency is not configured");
  }
  comet::ControllerStore store(db_path);
  store.Initialize();
  const auto desired_state =
      plane_name.has_value() ? store.LoadDesiredState(*plane_name)
                             : store.LoadDesiredState();
  const DiskStateViewData view{
      db_path,
      plane_name,
      node_name,
      desired_state,
      plane_name.has_value() ? store.LoadDesiredGeneration(*plane_name)
                             : store.LoadDesiredGeneration(),
      desired_state.has_value()
          ? store.LoadDiskRuntimeStates(desired_state->plane_name, node_name)
          : std::vector<comet::DiskRuntimeState>{},
      plane_name.has_value()
          ? deps_.filter_host_observations_for_plane(
                store.LoadHostObservations(node_name),
                *plane_name)
          : store.LoadHostObservations(node_name),
  };
  if (!view.desired_state.has_value()) {
    std::cout << "disk-state:\n";
    std::cout << "  (empty)\n";
    return 0;
  }

  std::cout << "db: " << view.db_path << "\n";
  if (view.plane_name.has_value()) {
    std::cout << "plane_filter: " << *view.plane_name << "\n";
  }
  if (view.node_name.has_value()) {
    std::cout << "node_filter: " << *view.node_name << "\n";
  }
  deps_.print_detailed_disk_state(
      *view.desired_state,
      view.runtime_states,
      view.observations,
      view.node_name);
  return 0;
}

}  // namespace comet::controller
