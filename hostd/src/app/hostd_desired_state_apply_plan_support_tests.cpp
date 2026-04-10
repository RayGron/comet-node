#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/hostd_command_support.h"
#include "app/hostd_compose_runtime_support.h"
#include "app/hostd_desired_state_apply_plan_support.h"
#include "app/hostd_desired_state_display_support.h"
#include "app/hostd_desired_state_path_support.h"
#include "app/hostd_disk_runtime_support.h"
#include "app/hostd_file_support.h"
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

comet::NodeExecutionPlan FindPlan(
    const std::vector<comet::NodeExecutionPlan>& plans,
    const std::string& node_name) {
  for (const auto& plan : plans) {
    if (plan.node_name == node_name) {
      return plan;
    }
  }
  throw std::runtime_error("plan not found for node: " + node_name);
}

}  // namespace

int main() {
  try {
    const comet::hostd::HostdCommandSupport command_support;
    const comet::hostd::HostdDesiredStatePathSupport path_support;
    const comet::hostd::HostdFileSupport file_support;
    const comet::hostd::HostdComposeRuntimeSupport compose_support(command_support);
    const comet::hostd::HostdDiskRuntimeSupport disk_support(
        command_support,
        path_support,
        file_support);
    const comet::hostd::HostdDesiredStateApplyPlanSupport support(
        command_support,
        compose_support,
        disk_support,
        file_support);

    {
      comet::NodeComposePlan compose_plan;
      compose_plan.plane_name = "alpha";
      compose_plan.node_name = "node-a";
      comet::ComposeService infer_service;
      infer_service.environment["COMET_INSTANCE_ROLE"] = "infer";
      comet::ComposeService worker_service;
      worker_service.environment["COMET_INSTANCE_ROLE"] = "worker";
      comet::ComposeService app_service;
      app_service.environment["COMET_INSTANCE_ROLE"] = "app";
      compose_plan.services = {infer_service, worker_service, app_service};
      Expect(
          support.ExpectedRuntimeStatusCountForComposePlan(compose_plan) == 2,
          "ExpectedRuntimeStatusCountForComposePlan should count infer and worker services only");
    }

    {
      namespace fs = std::filesystem;
      const fs::path temp_root =
          fs::temp_directory_path() / "comet-hostd-desired-state-apply-plan-support-tests";
      std::error_code cleanup_error;
      fs::remove_all(temp_root, cleanup_error);
      fs::create_directories(temp_root);

      const fs::path artifacts_root = temp_root / "artifacts";
      const std::string storage_root = "/mnt/test-storage";
      const std::optional<std::string> runtime_root = (temp_root / "runtime").string();

      const comet::DesiredState full_state =
          path_support.RebaseStateForRuntimeRoot(
              comet::BuildDemoState(),
              storage_root,
              runtime_root);
      const comet::DesiredState node_state = comet::SliceDesiredStateForNode(full_state, "node-a");
      const auto compose_plan = comet::FindNodeComposePlan(node_state, "node-a");
      Expect(compose_plan.has_value(), "demo node should have a compose plan");

      const auto execution_plan = FindPlan(
          comet::BuildNodeExecutionPlans(std::nullopt, node_state, artifacts_root.string()),
          "node-a");
      std::vector<std::string> phases;
      support.ApplyNodePlan(
          execution_plan,
          node_state,
          *compose_plan,
          storage_root,
          runtime_root,
          comet::hostd::ComposeMode::Skip,
          nullptr,
          [&](const std::string& phase,
              const std::string&,
              const std::string&,
              int,
              const std::string&,
              const std::string&) { phases.push_back(phase); });

      Expect(
          fs::exists(execution_plan.compose_file_path),
          "ApplyNodePlan should write the compose file");
      std::optional<std::string> runtime_config_path;
      for (const auto& operation : execution_plan.operations) {
        if (operation.kind == comet::HostOperationKind::WriteInferRuntimeConfig) {
          runtime_config_path = operation.target;
          break;
        }
      }
      Expect(runtime_config_path.has_value(), "demo node should have infer runtime config path");
      Expect(
          fs::exists(*runtime_config_path),
          "ApplyNodePlan should write the infer runtime config file");
      for (const auto& disk : node_state.disks) {
        Expect(fs::exists(disk.host_path), "ApplyNodePlan should realize desired disk paths");
      }
      Expect(
          std::find(phases.begin(), phases.end(), "rendering-runtime") != phases.end(),
          "ApplyNodePlan should publish rendering progress");
      Expect(
          std::find(phases.begin(), phases.end(), "starting-runtime") != phases.end(),
          "ApplyNodePlan should publish startup progress");

      fs::remove_all(temp_root, cleanup_error);
    }

    std::cout << "ok: hostd-desired-state-apply-plan-support-runtime-status-count\n";
    std::cout << "ok: hostd-desired-state-apply-plan-support-apply-node-plan\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }
}
