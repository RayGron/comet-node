#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "plane/dashboard_service.h"
#include "scheduler/scheduler_domain_service.h"
#include "scheduler/scheduler_view_service.h"

#include "comet/models.h"
#include "comet/sqlite_store.h"

namespace comet::controller {

class StateAggregateLoader {
 public:
  using FilterHostObservationsForPlaneFn =
      std::function<std::vector<comet::HostObservation>(
          const std::vector<comet::HostObservation>&,
          const std::string&)>;
  using LoadSchedulerRuntimeViewFn =
      std::function<SchedulerRuntimeView(
          comet::ControllerStore&,
          const std::optional<comet::DesiredState>&)>;
  using EvaluateSchedulingPolicyFn =
      std::function<comet::SchedulingPolicyReport(const comet::DesiredState&)>;
  using MaximumRebalanceIterationsFn = std::function<int()>;

  struct Deps {
    FilterHostObservationsForPlaneFn filter_host_observations_for_plane;
    LoadSchedulerRuntimeViewFn load_scheduler_runtime_view;
    EvaluateSchedulingPolicyFn evaluate_scheduling_policy;
    MaximumRebalanceIterationsFn maximum_rebalance_iterations;
    const SchedulerDomainService* scheduler_domain_service = nullptr;
    const SchedulerViewService* scheduler_view_service = nullptr;
  };

  explicit StateAggregateLoader(Deps deps);

  RolloutActionsViewData LoadRolloutActionsViewData(
      const std::string& db_path,
      const std::optional<std::string>& node_name,
      const std::optional<std::string>& plane_name) const;

  RebalancePlanViewData LoadRebalancePlanViewData(
      const std::string& db_path,
      const std::optional<std::string>& node_name,
      int stale_after_seconds,
      const std::optional<std::string>& plane_name) const;

  StateAggregateViewData LoadStateAggregateViewData(
      const std::string& db_path,
      int stale_after_seconds,
      const std::optional<std::string>& plane_name = std::nullopt) const;

 private:
  Deps deps_;
};

}  // namespace comet::controller
