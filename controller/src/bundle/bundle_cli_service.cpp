#include "bundle/bundle_cli_service.h"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "comet/planning/compose_renderer.h"
#include "comet/state/demo_state.h"
#include "comet/importing/import_bundle.h"
#include "comet/runtime/infer_runtime_config.h"
#include "comet/planning/planner.h"
#include "comet/planning/reconcile.h"
#include "comet/state/state_json.h"

namespace comet::controller {

namespace {

void PrintPreviewSummary(const comet::DesiredState& state) {
  std::cout << "preview:\n";
  std::cout << "  plane=" << state.plane_name << "\n";
  std::cout << "  nodes=" << state.nodes.size() << "\n";
  std::cout << "  disks=" << state.disks.size() << "\n";
  std::cout << "  instances=" << state.instances.size() << "\n";

  const auto node_plans = comet::BuildNodeComposePlans(state);
  for (const auto& plan : node_plans) {
    std::cout << "  node " << plan.node_name
              << ": services=" << plan.services.size()
              << " disks=" << plan.disks.size() << "\n";
  }
}

int RenderComposeForState(
    const comet::DesiredState& state,
    const std::optional<std::string>& node_name) {
  if (node_name.has_value()) {
    const auto plan = comet::FindNodeComposePlan(state, *node_name);
    if (!plan.has_value()) {
      std::cerr << "error: node '" << *node_name << "' not found in state\n";
      return 1;
    }
    std::cout << comet::RenderComposeYaml(*plan);
    return 0;
  }

  const auto plans = comet::BuildNodeComposePlans(state);
  for (std::size_t index = 0; index < plans.size(); ++index) {
    if (index > 0) {
      std::cout << "\n---\n";
    }
    std::cout << comet::RenderComposeYaml(plans[index]);
  }
  return 0;
}

std::vector<comet::NodeExecutionPlan> FilterNodeExecutionPlans(
    const std::vector<comet::NodeExecutionPlan>& plans,
    const std::optional<std::string>& node_name) {
  if (!node_name.has_value()) {
    return plans;
  }

  std::vector<comet::NodeExecutionPlan> filtered;
  for (const auto& plan : plans) {
    if (plan.node_name == *node_name) {
      filtered.push_back(plan);
    }
  }

  if (filtered.empty()) {
    throw std::runtime_error("node '" + *node_name + "' not found in execution plan");
  }

  return filtered;
}

}  // namespace

BundleCliService::BundleCliService(Deps deps) : deps_(std::move(deps)) {}

void BundleCliService::ShowDemoPlan() const {
  deps_.print_state_summary(comet::BuildDemoState());
}

int BundleCliService::RenderDemoCompose(
    const std::optional<std::string>& node_name) const {
  return RenderComposeForState(comet::BuildDemoState(), node_name);
}

int BundleCliService::InitDb(const std::string& db_path) const {
  comet::ControllerStore store(db_path);
  store.Initialize();
  std::cout << "initialized db: " << db_path << "\n";
  return 0;
}

int BundleCliService::ValidateBundle(const std::string& bundle_dir) const {
  const comet::DesiredState state = comet::ImportPlaneBundle(bundle_dir);
  comet::RequireSchedulingPolicy(state);
  const comet::SchedulingPolicyReport scheduling_report =
      comet::EvaluateSchedulingPolicy(state);
  std::cout << "bundle validation: OK\n";
  PrintPreviewSummary(state);
  std::cout << comet::RenderSchedulingPolicyReport(scheduling_report);
  deps_.print_scheduler_decision_summary(state);
  deps_.print_rollout_gate_summary(scheduling_report);
  return 0;
}

int BundleCliService::PreviewBundle(
    const std::string& bundle_dir,
    const std::optional<std::string>& node_name) const {
  const comet::DesiredState state = comet::ImportPlaneBundle(bundle_dir);
  const comet::SchedulingPolicyReport scheduling_report =
      comet::EvaluateSchedulingPolicy(state);
  PrintPreviewSummary(state);
  std::cout << comet::RenderSchedulingPolicyReport(scheduling_report);
  deps_.print_scheduler_decision_summary(state);
  deps_.print_rollout_gate_summary(scheduling_report);
  std::cout << "\ncompose-preview:\n";
  return RenderComposeForState(state, node_name);
}

int BundleCliService::PlanBundle(
    const std::string& db_path,
    const std::string& bundle_dir) const {
  comet::ControllerStore store(db_path);
  store.Initialize();
  const auto current_state = store.LoadDesiredState();
  const comet::DesiredState desired_state = comet::ImportPlaneBundle(bundle_dir);
  const auto availability_overrides = store.LoadNodeAvailabilityOverrides();
  const auto observations = store.LoadHostObservations();
  const comet::ReconcilePlan plan =
      comet::BuildReconcilePlan(current_state, desired_state);
  const comet::SchedulingPolicyReport scheduling_report =
      comet::EvaluateSchedulingPolicy(desired_state);

  std::cout << "bundle-plan:\n";
  std::cout << "  db=" << db_path << "\n";
  std::cout << "  bundle=" << bundle_dir << "\n";
  std::cout << comet::RenderReconcilePlan(plan);
  std::cout << comet::RenderSchedulingPolicyReport(scheduling_report);
  deps_.print_scheduler_decision_summary(desired_state);
  deps_.print_rollout_gate_summary(scheduling_report);
  deps_.print_assignment_dispatch_summary(
      desired_state,
      deps_.build_availability_override_map(availability_overrides),
      observations,
      deps_.default_stale_after_seconds());
  return 0;
}

int BundleCliService::PlanHostOps(
    const std::string& db_path,
    const std::string& bundle_dir,
    const std::string& artifacts_root,
    const std::optional<std::string>& node_name) const {
  comet::ControllerStore store(db_path);
  store.Initialize();
  const auto current_state = store.LoadDesiredState();
  const comet::DesiredState desired_state = comet::ImportPlaneBundle(bundle_dir);
  const auto host_plans =
      FilterNodeExecutionPlans(
          comet::BuildNodeExecutionPlans(current_state, desired_state, artifacts_root),
          node_name);

  std::cout << "host-op-plan:\n";
  std::cout << "  db=" << db_path << "\n";
  std::cout << "  bundle=" << bundle_dir << "\n";
  std::cout << "  artifacts_root=" << artifacts_root << "\n";
  std::cout << comet::RenderNodeExecutionPlans(host_plans);
  return 0;
}

int BundleCliService::SeedDemo(const std::string& db_path) const {
  comet::ControllerStore store(db_path);
  store.Initialize();
  const comet::DesiredState desired_state = comet::BuildDemoState();
  comet::RequireSchedulingPolicy(desired_state);
  const comet::SchedulingPolicyReport scheduling_report =
      comet::EvaluateSchedulingPolicy(desired_state);
  const auto availability_overrides = store.LoadNodeAvailabilityOverrides();
  const auto observations = store.LoadHostObservations();
  const int desired_generation =
      store.LoadDesiredGeneration(desired_state.plane_name).value_or(0) + 1;
  store.ReplaceDesiredState(desired_state, desired_generation, 0);
  store.ClearSchedulerPlaneRuntime(desired_state.plane_name);
  store.ReplaceRolloutActions(
      desired_state.plane_name, desired_generation, scheduling_report.rollout_actions);
  store.ReplaceHostAssignments(
      deps_.build_host_assignments(
          desired_state,
          deps_.default_artifacts_root(),
          desired_generation,
          availability_overrides,
          observations,
          scheduling_report));
  std::cout << "seeded demo state into: " << db_path << "\n";
  std::cout << "desired generation: " << desired_generation << "\n";
  deps_.print_scheduler_decision_summary(desired_state);
  deps_.print_rollout_gate_summary(scheduling_report);
  deps_.print_assignment_dispatch_summary(
      desired_state,
      deps_.build_availability_override_map(availability_overrides),
      observations,
      deps_.default_stale_after_seconds());
  return 0;
}

int BundleCliService::ImportBundle(
    const std::string& db_path,
    const std::string& bundle_dir) const {
  comet::ControllerStore store(db_path);
  store.Initialize();
  const comet::DesiredState desired_state = comet::ImportPlaneBundle(bundle_dir);
  comet::RequireSchedulingPolicy(desired_state);
  const int desired_generation =
      store.LoadDesiredGeneration(desired_state.plane_name).value_or(0) + 1;
  store.ReplaceDesiredState(desired_state, desired_generation, 0);
  store.UpdatePlaneArtifactsRoot(desired_state.plane_name, deps_.default_artifacts_root());
  store.ClearSchedulerPlaneRuntime(desired_state.plane_name);
  store.ReplaceRolloutActions(desired_state.plane_name, desired_generation, {});
  deps_.event_appender(
      store,
      "bundle",
      "imported",
      "imported bundle into desired state; rollout is staged until explicit start",
      nlohmann::json{
          {"bundle_dir", bundle_dir},
          {"desired_generation", desired_generation},
          {"worker_count", desired_state.instances.size()},
          {"disk_count", desired_state.disks.size()},
      },
      desired_state.plane_name,
      "",
      "",
      std::nullopt,
      std::nullopt,
      "info");
  std::cout << "imported bundle '" << bundle_dir << "' into: " << db_path << "\n";
  std::cout << "desired generation: " << desired_generation << "\n";
  std::cout << "runtime rollout is staged; use start-plane to enqueue host assignments\n";
  return 0;
}

int BundleCliService::ApplyBundle(
    const std::string& db_path,
    const std::string& bundle_dir,
    const std::string& artifacts_root) const {
  comet::ControllerStore store(db_path);
  store.Initialize();
  const comet::DesiredState desired_state = comet::ImportPlaneBundle(bundle_dir);
  const auto current_state = store.LoadDesiredState(desired_state.plane_name);
  comet::RequireSchedulingPolicy(desired_state);
  const int desired_generation =
      store.LoadDesiredGeneration(desired_state.plane_name).value_or(0) + 1;
  const comet::ReconcilePlan plan =
      comet::BuildReconcilePlan(current_state, desired_state);
  const comet::SchedulingPolicyReport scheduling_report =
      comet::EvaluateSchedulingPolicy(desired_state);
  const auto host_plans =
      comet::BuildNodeExecutionPlans(current_state, desired_state, artifacts_root);

  std::cout << "apply-plan:\n";
  std::cout << comet::RenderReconcilePlan(plan);
  std::cout << comet::RenderSchedulingPolicyReport(scheduling_report);
  deps_.print_scheduler_decision_summary(desired_state);
  deps_.print_rollout_gate_summary(scheduling_report);
  std::cout << comet::RenderNodeExecutionPlans(host_plans);

  deps_.materialize_compose_artifacts(desired_state, host_plans);
  deps_.materialize_infer_runtime_artifact(desired_state, artifacts_root);
  store.ReplaceDesiredState(desired_state, desired_generation, 0);
  store.UpdatePlaneArtifactsRoot(desired_state.plane_name, artifacts_root);
  store.ClearSchedulerPlaneRuntime(desired_state.plane_name);
  store.ReplaceRolloutActions(desired_state.plane_name, desired_generation, {});
  deps_.event_appender(
      store,
      "bundle",
      "applied",
      "applied bundle into desired state; rollout is staged until explicit start",
      nlohmann::json{
          {"bundle_dir", bundle_dir},
          {"artifacts_root", artifacts_root},
          {"desired_generation", desired_generation},
          {"worker_count", desired_state.instances.size()},
          {"disk_count", desired_state.disks.size()},
      },
      desired_state.plane_name,
      "",
      "",
      std::nullopt,
      std::nullopt,
      "info");
  std::cout << "applied bundle '" << bundle_dir << "' into: " << db_path << "\n";
  std::cout << "desired generation: " << desired_generation << "\n";
  std::cout << "artifacts written under: " << artifacts_root << "\n";
  std::cout << "runtime rollout is staged; use start-plane to enqueue host assignments\n";
  return 0;
}

int BundleCliService::ApplyDesiredState(
    const std::string& db_path,
    const comet::DesiredState& desired_state,
    const std::string& artifacts_root,
    const std::string& source_label) const {
  comet::ControllerStore store(db_path);
  store.Initialize();
  comet::DesiredState effective_desired_state = desired_state;
  deps_.apply_registered_host_execution_modes(store, &effective_desired_state);
  deps_.resolve_desired_state_dynamic_placements(store, &effective_desired_state);
  deps_.validate_desired_state_for_controller_admission(effective_desired_state);
  deps_.validate_desired_state_execution_modes(effective_desired_state);
  const auto current_state = store.LoadDesiredState(effective_desired_state.plane_name);
  comet::RequireSchedulingPolicy(effective_desired_state);
  const auto availability_overrides = store.LoadNodeAvailabilityOverrides();
  const auto observations = store.LoadHostObservations();
  const int desired_generation =
      store.LoadDesiredGeneration(effective_desired_state.plane_name).value_or(0) + 1;
  const comet::ReconcilePlan plan =
      comet::BuildReconcilePlan(current_state, effective_desired_state);
  const comet::SchedulingPolicyReport scheduling_report =
      comet::EvaluateSchedulingPolicy(effective_desired_state);
  const auto host_plans =
      comet::BuildNodeExecutionPlans(current_state, effective_desired_state, artifacts_root);

  std::cout << "apply-plan:\n";
  std::cout << comet::RenderReconcilePlan(plan);
  std::cout << comet::RenderSchedulingPolicyReport(scheduling_report);
  deps_.print_scheduler_decision_summary(effective_desired_state);
  deps_.print_rollout_gate_summary(scheduling_report);
  std::cout << comet::RenderNodeExecutionPlans(host_plans);

  deps_.materialize_compose_artifacts(effective_desired_state, host_plans);
  deps_.materialize_infer_runtime_artifact(effective_desired_state, artifacts_root);
  store.ReplaceDesiredState(effective_desired_state, desired_generation, 0);
  store.UpdatePlaneArtifactsRoot(effective_desired_state.plane_name, artifacts_root);
  store.ClearSchedulerPlaneRuntime(effective_desired_state.plane_name);
  store.ReplaceRolloutActions(effective_desired_state.plane_name, desired_generation, {});

  const bool existed = current_state.has_value();
  deps_.event_appender(
      store,
      "plane",
      existed ? "staged-update" : "created",
      existed ? "updated plane desired state; rollout is staged until explicit restart"
             : "created plane desired state in stopped lifecycle state",
      nlohmann::json{
          {"source", source_label},
          {"artifacts_root", artifacts_root},
          {"desired_generation", desired_generation},
          {"applied_generation",
           current_state.has_value()
               ? nlohmann::json(store.LoadPlane(effective_desired_state.plane_name)->applied_generation)
               : nlohmann::json(0)},
          {"worker_count", effective_desired_state.instances.size()},
          {"disk_count", effective_desired_state.disks.size()},
      },
      effective_desired_state.plane_name,
      "",
      "",
      std::nullopt,
      std::nullopt,
      "info");
  std::cout << (existed ? "staged update for" : "created") << " plane '"
            << effective_desired_state.plane_name
            << "' in: " << db_path << "\n";
  std::cout << "desired generation: " << desired_generation << "\n";
  const auto plane = store.LoadPlane(effective_desired_state.plane_name);
  if (plane.has_value()) {
    std::cout << "applied generation: " << plane->applied_generation << "\n";
  }
  std::cout << "runtime rollout is staged; use start-plane to enqueue host assignments\n";
  return 0;
}

int BundleCliService::ApplyStateFile(
    const std::string& db_path,
    const std::string& state_path,
    const std::string& artifacts_root) const {
  const auto desired_state = comet::LoadDesiredStateJson(state_path);
  if (!desired_state.has_value()) {
    throw std::runtime_error("failed to load desired state file '" + state_path + "'");
  }
  return ApplyDesiredState(
      db_path,
      *desired_state,
      artifacts_root,
      "state-file:" + state_path);
}

int BundleCliService::RenderCompose(
    const std::string& db_path,
    const std::optional<std::string>& node_name) const {
  comet::ControllerStore store(db_path);
  store.Initialize();
  const auto state = store.LoadDesiredState();
  if (!state.has_value()) {
    std::cerr << "error: no desired state found in db '" << db_path << "'\n";
    return 1;
  }
  return RenderComposeForState(*state, node_name);
}

int BundleCliService::RenderInferRuntime(const std::string& db_path) const {
  comet::ControllerStore store(db_path);
  store.Initialize();
  const auto state = store.LoadDesiredState();
  if (!state.has_value()) {
    std::cerr << "error: no desired state found in db '" << db_path << "'\n";
    return 1;
  }
  std::cout << comet::RenderInferRuntimeConfigJson(*state) << "\n";
  return 0;
}

ControllerActionResult BundleCliService::ExecuteValidateBundleAction(
    const std::string& bundle_dir) const {
  return RunControllerActionResult(
      "validate-bundle",
      [&]() { return ValidateBundle(bundle_dir); });
}

ControllerActionResult BundleCliService::ExecutePreviewBundleAction(
    const std::string& bundle_dir,
    const std::optional<std::string>& node_name) const {
  return RunControllerActionResult(
      "preview-bundle",
      [&]() { return PreviewBundle(bundle_dir, node_name); });
}

ControllerActionResult BundleCliService::ExecuteImportBundleAction(
    const std::string& db_path,
    const std::string& bundle_dir) const {
  return RunControllerActionResult(
      "import-bundle",
      [&]() { return ImportBundle(db_path, bundle_dir); });
}

ControllerActionResult BundleCliService::ExecuteApplyBundleAction(
    const std::string& db_path,
    const std::string& bundle_dir,
    const std::string& artifacts_root) const {
  return RunControllerActionResult(
      "apply-bundle",
      [&]() { return ApplyBundle(db_path, bundle_dir, artifacts_root); });
}

}  // namespace comet::controller
