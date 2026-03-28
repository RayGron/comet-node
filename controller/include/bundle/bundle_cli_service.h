#pragma once

#include <functional>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "infra/controller_action.h"
#include "app/controller_service_interfaces.h"

#include "comet/planning/execution_plan.h"
#include "comet/state/models.h"
#include "comet/planning/scheduling_policy.h"
#include "comet/state/sqlite_store.h"

namespace comet::controller {

class BundleCliService : public IBundleCliService {
 public:
  using DefaultArtifactsRootProvider = std::function<std::string()>;
  using DefaultStaleAfterSecondsFn = std::function<int()>;
  using BuildAvailabilityOverrideMapFn =
      std::function<std::map<std::string, comet::NodeAvailabilityOverride>(
          const std::vector<comet::NodeAvailabilityOverride>&)>;
  using PrintStateSummaryFn = std::function<void(const comet::DesiredState&)>;
  using PrintSchedulerDecisionSummaryFn =
      std::function<void(const comet::DesiredState&)>;
  using PrintRolloutGateSummaryFn =
      std::function<void(const comet::SchedulingPolicyReport&)>;
  using PrintAssignmentDispatchSummaryFn = std::function<void(
      const comet::DesiredState&,
      const std::map<std::string, comet::NodeAvailabilityOverride>&,
      const std::vector<comet::HostObservation>&,
      int)>;
  using MaterializeComposeArtifactsFn =
      std::function<void(
          const comet::DesiredState&,
          const std::vector<comet::NodeExecutionPlan>&)>;
  using MaterializeInferRuntimeArtifactFn =
      std::function<void(const comet::DesiredState&, const std::string&)>;
  using BuildHostAssignmentsFn =
      std::function<std::vector<comet::HostAssignment>(
          const comet::DesiredState&,
          const std::string&,
          int,
          const std::vector<comet::NodeAvailabilityOverride>&,
          const std::vector<comet::HostObservation>&,
          const std::optional<comet::SchedulingPolicyReport>&)>;
  using ApplyRegisteredHostExecutionModesFn =
      std::function<void(comet::ControllerStore&, comet::DesiredState*)>;
  using ResolveDesiredStateDynamicPlacementsFn =
      std::function<void(comet::ControllerStore&, comet::DesiredState*)>;
  using ValidateDesiredStateForControllerAdmissionFn =
      std::function<void(const comet::DesiredState&)>;
  using ValidateDesiredStateExecutionModesFn =
      std::function<void(const comet::DesiredState&)>;
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

  struct Deps {
    DefaultArtifactsRootProvider default_artifacts_root;
    DefaultStaleAfterSecondsFn default_stale_after_seconds;
    BuildAvailabilityOverrideMapFn build_availability_override_map;
    PrintStateSummaryFn print_state_summary;
    PrintSchedulerDecisionSummaryFn print_scheduler_decision_summary;
    PrintRolloutGateSummaryFn print_rollout_gate_summary;
    PrintAssignmentDispatchSummaryFn print_assignment_dispatch_summary;
    MaterializeComposeArtifactsFn materialize_compose_artifacts;
    MaterializeInferRuntimeArtifactFn materialize_infer_runtime_artifact;
    BuildHostAssignmentsFn build_host_assignments;
    ApplyRegisteredHostExecutionModesFn apply_registered_host_execution_modes;
    ResolveDesiredStateDynamicPlacementsFn resolve_desired_state_dynamic_placements;
    ValidateDesiredStateForControllerAdmissionFn
        validate_desired_state_for_controller_admission;
    ValidateDesiredStateExecutionModesFn validate_desired_state_execution_modes;
    EventAppender event_appender;
  };

  explicit BundleCliService(Deps deps);

  void ShowDemoPlan() const override;
  int RenderDemoCompose(const std::optional<std::string>& node_name) const override;
  int InitDb(const std::string& db_path) const override;
  int ValidateBundle(const std::string& bundle_dir) const override;
  int PreviewBundle(
      const std::string& bundle_dir,
      const std::optional<std::string>& node_name) const override;
  int PlanBundle(
      const std::string& db_path,
      const std::string& bundle_dir) const override;
  int PlanHostOps(
      const std::string& db_path,
      const std::string& bundle_dir,
      const std::string& artifacts_root,
      const std::optional<std::string>& node_name) const override;
  int SeedDemo(const std::string& db_path) const override;
  int ImportBundle(
      const std::string& db_path,
      const std::string& bundle_dir) const override;
  int ApplyBundle(
      const std::string& db_path,
      const std::string& bundle_dir,
      const std::string& artifacts_root) const override;
  int ApplyDesiredState(
      const std::string& db_path,
      const comet::DesiredState& desired_state,
      const std::string& artifacts_root,
      const std::string& source_label) const;
  int ApplyStateFile(
      const std::string& db_path,
      const std::string& state_path,
      const std::string& artifacts_root) const override;
  int RenderCompose(
      const std::string& db_path,
      const std::optional<std::string>& node_name) const override;
  int RenderInferRuntime(const std::string& db_path) const override;

  ControllerActionResult ExecuteValidateBundleAction(const std::string& bundle_dir) const;
  ControllerActionResult ExecutePreviewBundleAction(
      const std::string& bundle_dir,
      const std::optional<std::string>& node_name) const;
  ControllerActionResult ExecuteImportBundleAction(
      const std::string& db_path,
      const std::string& bundle_dir) const;
  ControllerActionResult ExecuteApplyBundleAction(
      const std::string& db_path,
      const std::string& bundle_dir,
      const std::string& artifacts_root) const;

 private:
  Deps deps_;
};

}  // namespace comet::controller
