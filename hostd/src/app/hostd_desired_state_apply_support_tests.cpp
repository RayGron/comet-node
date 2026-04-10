#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/hostd_bootstrap_model_support.h"
#include "app/hostd_bootstrap_model_support_factory.h"
#include "app/hostd_command_support.h"
#include "app/hostd_compose_runtime_support.h"
#include "app/hostd_desired_state_apply_plan_support.h"
#include "app/hostd_desired_state_apply_support.h"
#include "app/hostd_desired_state_display_support.h"
#include "app/hostd_desired_state_path_support.h"
#include "app/hostd_disk_runtime_support.h"
#include "app/hostd_file_support.h"
#include "app/hostd_local_runtime_state_support.h"
#include "app/hostd_local_state_path_support.h"
#include "app/hostd_local_state_repository.h"
#include "app/hostd_post_deploy_support.h"
#include "app/hostd_reporting_support.h"
#include "comet/planning/execution_plan.h"
#include "comet/planning/planner.h"
#include "comet/state/demo_state.h"
#include "comet/state/state_json.h"

namespace {

void Expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

}  // namespace

int main() {
  try {
    namespace fs = std::filesystem;

    const comet::hostd::HostdCommandSupport command_support;
    const comet::hostd::HostdDesiredStatePathSupport path_support;
    const comet::hostd::HostdRuntimeTelemetrySupport runtime_telemetry_support;
    const comet::hostd::HostdLocalStatePathSupport local_state_path_support;
    const comet::hostd::HostdLocalStateRepository local_state_repository(local_state_path_support);
    const comet::hostd::HostdLocalRuntimeStateSupport local_runtime_state_support(
        path_support,
        local_state_repository,
        runtime_telemetry_support);
    const comet::hostd::HostdFileSupport file_support;
    const comet::hostd::HostdReportingSupport reporting_support;
    const comet::hostd::HostdComposeRuntimeSupport compose_support(command_support);
    const comet::hostd::HostdDesiredStateDisplaySupport display_support(path_support);
    const comet::hostd::HostdDiskRuntimeSupport disk_support(
        command_support,
        path_support,
        file_support);
    const comet::hostd::HostdDesiredStateApplyPlanSupport apply_plan_support(
        command_support,
        compose_support,
        disk_support,
        file_support);
    const comet::hostd::HostdPostDeploySupport post_deploy_support(command_support);
    const comet::hostd::HostdBootstrapModelSupportFactory bootstrap_support_factory(
        path_support,
        command_support,
        file_support,
        reporting_support);
    const auto bootstrap_support = bootstrap_support_factory.Create();
    const comet::hostd::HostdDesiredStateApplySupport support(
        path_support,
        display_support,
        apply_plan_support,
        disk_support,
        post_deploy_support,
        local_state_repository,
        local_runtime_state_support,
        bootstrap_support);

    const fs::path temp_root =
        fs::temp_directory_path() / "comet-hostd-desired-state-apply-support-tests";
    std::error_code cleanup_error;
    fs::remove_all(temp_root, cleanup_error);
    fs::create_directories(temp_root);

    const std::string storage_root = "/mnt/test-storage";
    const std::optional<std::string> runtime_root = (temp_root / "runtime").string();
    const std::string state_root = (temp_root / "state").string();
    const std::string artifacts_root = (temp_root / "artifacts").string();

    const comet::DesiredState full_state =
        path_support.RebaseStateForRuntimeRoot(
            comet::BuildDemoState(),
            storage_root,
            runtime_root);
    const comet::DesiredState node_state = comet::SliceDesiredStateForNode(full_state, "node-a");
    const std::string node_name = "node-a";
    const std::string plane_name = node_state.plane_name;
    const auto compose_plan = comet::FindNodeComposePlan(node_state, node_name);
    Expect(compose_plan.has_value(), "demo node should have a compose plan");
    const auto execution_plans =
        comet::BuildNodeExecutionPlans(std::nullopt, node_state, artifacts_root);
    std::optional<comet::NodeExecutionPlan> execution_plan;
    for (const auto& plan : execution_plans) {
      if (plan.node_name == node_name) {
        execution_plan = plan;
        break;
      }
    }
    Expect(execution_plan.has_value(), "demo node should have an execution plan");

    apply_plan_support.ApplyNodePlan(
        *execution_plan,
        node_state,
        *compose_plan,
        storage_root,
        runtime_root,
        comet::hostd::ComposeMode::Skip,
        nullptr,
        {});

    local_state_repository.SaveLocalAppliedState(
        state_root,
        node_name,
        node_state,
        plane_name);
    local_state_repository.SaveLocalAppliedGeneration(
        state_root,
        node_name,
        5,
        plane_name);
    local_state_repository.RewriteAggregateLocalState(state_root, node_name);
    local_state_repository.RewriteAggregateLocalGeneration(state_root, node_name);

    std::vector<std::string> phases;
    support.ApplyDesiredNodeState(
        node_state,
        artifacts_root,
        storage_root,
        runtime_root,
        state_root,
        comet::hostd::ComposeMode::Skip,
        "test-apply-support",
        7,
        42,
        nullptr,
        [&](const std::string& phase,
            const std::string&,
            const std::string&,
            int,
            const std::string&,
            const std::string&) {
          phases.push_back(phase);
        });

    const auto persisted_state =
        local_state_repository.LoadLocalAppliedState(state_root, node_name, plane_name);
    Expect(
        persisted_state.has_value(),
        "ApplyDesiredNodeState should keep local state after apply cycle");
    const auto generation =
        local_state_repository.LoadLocalAppliedGeneration(state_root, node_name, plane_name);
    Expect(generation.has_value() && *generation == 7, "ApplyDesiredNodeState should update generation");
    Expect(
        !phases.empty() && phases.back() == "completed",
        "ApplyDesiredNodeState should publish completed progress");

    fs::remove_all(temp_root, cleanup_error);

    std::cout << "ok: hostd-desired-state-apply-support-progress-and-generation\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }
}
