#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "scheduler/scheduler_domain_support.h"
#include "scheduler/scheduler_view_service.h"

#include "comet/state/models.h"
#include "comet/runtime/runtime_status.h"
#include "comet/planning/scheduling_policy.h"
#include "comet/state/sqlite_store.h"

namespace comet::controller {

struct SchedulerDomainPolicyConfig {
  int default_stale_after_seconds = 300;
  int minimum_safe_direct_rebalance_score = 100;
  int worker_minimum_residency_seconds = 300;
  int node_cooldown_after_move_seconds = 60;
  int compute_pressure_utilization_threshold_pct = 85;
  int observed_move_vram_reserve_mb = 1024;
};

class SchedulerDomainService {
 public:
  SchedulerDomainService(
      std::shared_ptr<const SchedulerDomainSupport> domain_support,
      SchedulerDomainPolicyConfig policy_config = {});

  std::vector<RolloutLifecycleEntry> BuildRolloutLifecycleEntries(
      const comet::DesiredState& desired_state,
      int desired_generation,
      const std::vector<comet::RolloutActionRecord>& rollout_actions,
      const std::vector<comet::HostAssignment>& assignments,
      const std::vector<comet::HostObservation>& observations) const;

  RebalanceControllerGateSummary BuildRebalanceControllerGateSummary(
      const comet::DesiredState& desired_state,
      int desired_generation,
      const std::vector<comet::NodeAvailabilityOverride>& availability_overrides,
      const std::vector<RolloutLifecycleEntry>& rollout_lifecycle_entries,
      const std::vector<comet::HostAssignment>& assignments,
      const SchedulerRuntimeView& scheduler_runtime,
      const std::vector<comet::HostObservation>& observations,
      int stale_after_seconds) const;

  std::vector<RebalancePlanEntry> BuildRebalancePlanEntries(
      const comet::DesiredState& state,
      const comet::SchedulingPolicyReport& scheduling_report,
      const std::vector<comet::NodeAvailabilityOverride>& availability_overrides,
      const std::vector<RolloutLifecycleEntry>& rollout_lifecycle_entries,
      const std::vector<comet::HostAssignment>& assignments,
      const SchedulerRuntimeView& scheduler_runtime,
      const std::vector<comet::HostObservation>& observations,
      int stale_after_seconds,
      const std::optional<std::string>& node_name_filter = std::nullopt) const;

 private:
  std::shared_ptr<const SchedulerDomainSupport> domain_support_;
  SchedulerDomainPolicyConfig policy_config_;
};

}  // namespace comet::controller
