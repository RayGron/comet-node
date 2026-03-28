#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "comet/models.h"
#include "comet/runtime_status.h"
#include "comet/sqlite_store.h"

namespace comet::controller {

class ControllerPrintService {
 public:
  using BuildAvailabilityOverrideMapFn =
      std::function<std::map<std::string, comet::NodeAvailabilityOverride>(
          const std::vector<comet::NodeAvailabilityOverride>&)>;
  using ResolveNodeAvailabilityFn = std::function<comet::NodeAvailability(
      const std::map<std::string, comet::NodeAvailabilityOverride>&,
      const std::string&)>;
  using FindHostObservationForNodeFn =
      std::function<std::optional<comet::HostObservation>(
          const std::vector<comet::HostObservation>&,
          const std::string&)>;
  using HeartbeatAgeSecondsFn =
      std::function<std::optional<long long>(const std::string&)>;
  using HealthFromAgeFn = std::function<std::string(
      const std::optional<long long>&,
      int)>;
  using ParseRuntimeStatusFn =
      std::function<std::optional<comet::RuntimeStatus>(
          const comet::HostObservation&)>;
  using ParseInstanceRuntimeStatusesFn =
      std::function<std::vector<comet::RuntimeProcessStatus>(
          const comet::HostObservation&)>;
  using ParseGpuTelemetryFn =
      std::function<std::optional<comet::GpuTelemetrySnapshot>(
          const comet::HostObservation&)>;
  using ParseDiskTelemetryFn =
      std::function<std::optional<comet::DiskTelemetrySnapshot>(
          const comet::HostObservation&)>;
  using ParseNetworkTelemetryFn =
      std::function<std::optional<comet::NetworkTelemetrySnapshot>(
          const comet::HostObservation&)>;
  using ParseCpuTelemetryFn =
      std::function<std::optional<comet::CpuTelemetrySnapshot>(
          const comet::HostObservation&)>;
  using FormatDisplayTimestampFn = std::function<std::string(const std::string&)>;

  struct Deps {
    BuildAvailabilityOverrideMapFn build_availability_override_map;
    ResolveNodeAvailabilityFn resolve_node_availability;
    FindHostObservationForNodeFn find_host_observation_for_node;
    HeartbeatAgeSecondsFn heartbeat_age_seconds;
    HealthFromAgeFn health_from_age;
    ParseRuntimeStatusFn parse_runtime_status;
    ParseInstanceRuntimeStatusesFn parse_instance_runtime_statuses;
    ParseGpuTelemetryFn parse_gpu_telemetry;
    ParseDiskTelemetryFn parse_disk_telemetry;
    ParseNetworkTelemetryFn parse_network_telemetry;
    ParseCpuTelemetryFn parse_cpu_telemetry;
    FormatDisplayTimestampFn format_display_timestamp;
  };

  explicit ControllerPrintService(Deps deps);

  void PrintStateSummary(const comet::DesiredState& state) const;
  void PrintDiskRuntimeStates(const std::vector<comet::DiskRuntimeState>& runtime_states) const;
  void PrintDetailedDiskState(
      const comet::DesiredState& state,
      const std::vector<comet::DiskRuntimeState>& runtime_states,
      const std::vector<comet::HostObservation>& observations,
      const std::optional<std::string>& node_name) const;
  void PrintSchedulerDecisionSummary(const comet::DesiredState& state) const;
  void PrintRolloutGateSummary(const comet::SchedulingPolicyReport& scheduling_report) const;
  void PrintPersistedRolloutActions(
      const std::vector<comet::RolloutActionRecord>& actions) const;
  void PrintNodeAvailabilityOverrides(
      const std::vector<comet::NodeAvailabilityOverride>& availability_overrides) const;
  void PrintAssignmentDispatchSummary(
      const comet::DesiredState& desired_state,
      const std::map<std::string, comet::NodeAvailabilityOverride>& availability_overrides,
      const std::vector<comet::HostObservation>& observations,
      int stale_after_seconds) const;
  void PrintHostAssignments(
      const std::vector<comet::HostAssignment>& assignments) const;
  void PrintHostObservations(
      const std::vector<comet::HostObservation>& observations,
      int stale_after_seconds) const;
  void PrintHostHealth(
      const std::optional<comet::DesiredState>& desired_state,
      const std::vector<comet::HostObservation>& observations,
      const std::vector<comet::NodeAvailabilityOverride>& availability_overrides,
      const std::optional<std::string>& node_name,
      int stale_after_seconds) const;
  void PrintEvents(const std::vector<comet::EventRecord>& events) const;

 private:
  bool IsNodeSchedulable(comet::NodeAvailability availability) const;
  std::optional<std::string> ObservedSchedulingGateReason(
      const std::vector<comet::HostObservation>& observations,
      const std::string& node_name,
      int stale_after_seconds) const;

  Deps deps_;
};

}  // namespace comet::controller
