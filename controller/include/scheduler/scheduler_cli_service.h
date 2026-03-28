#pragma once

#include <functional>
#include <optional>
#include <string>

#include "scheduler/scheduler_view_service.h"
#include "read_model/state_aggregate_loader.h"

namespace comet::controller {

class SchedulerCliService {
 public:
  using PrintPersistedRolloutActionsFn =
      std::function<void(const std::vector<comet::RolloutActionRecord>&)>;
  using VerificationStableSamplesRequiredFn = std::function<int()>;

  struct Deps {
    const StateAggregateLoader* state_aggregate_loader = nullptr;
    const SchedulerViewService* scheduler_view_service = nullptr;
    PrintPersistedRolloutActionsFn print_persisted_rollout_actions;
    VerificationStableSamplesRequiredFn verification_stable_samples_required;
    int default_stale_after_seconds = 300;
  };

  explicit SchedulerCliService(Deps deps);

  int ShowRolloutActions(
      const std::string& db_path,
      const std::optional<std::string>& node_name,
      const std::optional<std::string>& plane_name) const;

  int ShowRebalancePlan(
      const std::string& db_path,
      const std::optional<std::string>& node_name,
      const std::optional<std::string>& plane_name) const;

 private:
  Deps deps_;
};

}  // namespace comet::controller
