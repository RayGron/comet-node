#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "scheduler/scheduler_view_service.h"

#include "comet/state/models.h"
#include "comet/runtime/runtime_status.h"
#include "comet/planning/scheduling_policy.h"
#include "comet/state/sqlite_store.h"

namespace comet::controller {

class SchedulerDomainService {
 public:
  using HeartbeatAgeSecondsFn =
      std::function<std::optional<long long>(const std::string&)>;
  using HealthFromAgeFn =
      std::function<std::string(const std::optional<long long>&, int)>;
  using ParseRuntimeStatusFn =
      std::function<std::optional<comet::RuntimeStatus>(
          const comet::HostObservation&)>;
  using ParseGpuTelemetryFn =
      std::function<std::optional<comet::GpuTelemetrySnapshot>(
          const comet::HostObservation&)>;
  using BuildAvailabilityOverrideMapFn =
      std::function<std::map<std::string, comet::NodeAvailabilityOverride>(
          const std::vector<comet::NodeAvailabilityOverride>&)>;
  using ResolveNodeAvailabilityFn =
      std::function<comet::NodeAvailability(
          const std::map<std::string, comet::NodeAvailabilityOverride>&,
          const std::string&)>;
  using IsNodeSchedulableFn =
      std::function<bool(comet::NodeAvailability)>;
  using TimestampAgeSecondsFn =
      std::function<std::optional<long long>(const std::string&)>;
  using ObservedSchedulingGateReasonFn =
      std::function<std::optional<std::string>(
          const std::vector<comet::HostObservation>&,
          const std::string&,
          int)>;

  struct Deps {
    HeartbeatAgeSecondsFn heartbeat_age_seconds;
    HealthFromAgeFn health_from_age;
    ParseRuntimeStatusFn parse_runtime_status;
    ParseGpuTelemetryFn parse_gpu_telemetry;
    BuildAvailabilityOverrideMapFn build_availability_override_map;
    ResolveNodeAvailabilityFn resolve_node_availability;
    IsNodeSchedulableFn is_node_schedulable;
    TimestampAgeSecondsFn timestamp_age_seconds;
    ObservedSchedulingGateReasonFn observed_scheduling_gate_reason;
    int default_stale_after_seconds = 300;
    int minimum_safe_direct_rebalance_score = 100;
    int worker_minimum_residency_seconds = 300;
    int node_cooldown_after_move_seconds = 60;
    int compute_pressure_utilization_threshold_pct = 85;
    int observed_move_vram_reserve_mb = 1024;
  };

  explicit SchedulerDomainService(Deps deps);

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
  Deps deps_;
};

}  // namespace comet::controller
