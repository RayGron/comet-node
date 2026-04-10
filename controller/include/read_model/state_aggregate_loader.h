#pragma once

#include <optional>
#include <string>
#include <vector>

#include "infra/controller_runtime_support_service.h"
#include "observation/plane_observation_matcher.h"
#include "plane/dashboard_service.h"
#include "scheduler/scheduler_domain_service.h"
#include "scheduler/scheduler_view_service.h"

#include "comet/state/models.h"
#include "comet/state/sqlite_store.h"

namespace comet::controller {

class StateAggregateLoader {
 public:
  StateAggregateLoader(
      const SchedulerDomainService& scheduler_domain_service,
      const SchedulerViewService& scheduler_view_service,
      ControllerRuntimeSupportService runtime_support_service,
      int maximum_rebalance_iterations);

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
  SchedulerRuntimeView LoadSchedulerRuntimeView(
      comet::ControllerStore& store,
      const std::optional<comet::DesiredState>& desired_state) const;

  const SchedulerDomainService& scheduler_domain_service_;
  const SchedulerViewService& scheduler_view_service_;
  ControllerRuntimeSupportService runtime_support_service_;
  PlaneObservationMatcher plane_observation_matcher_;
  int maximum_rebalance_iterations_ = 1;
};

}  // namespace comet::controller
