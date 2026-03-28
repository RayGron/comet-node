#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "comet/core/platform_compat.h"
#include "comet/runtime/runtime_status.h"
#include "comet/state/sqlite_store.h"

namespace comet::controller {

class ControllerRuntimeSupportService {
 public:
  std::map<std::string, comet::NodeAvailabilityOverride> BuildAvailabilityOverrideMap(
      const std::vector<comet::NodeAvailabilityOverride>& availability_overrides) const;

  comet::NodeAvailability ResolveNodeAvailability(
      const std::map<std::string, comet::NodeAvailabilityOverride>& availability_overrides,
      const std::string& node_name) const;

  std::optional<comet::HostObservation> FindHostObservationForNode(
      const std::vector<comet::HostObservation>& observations,
      const std::string& node_name) const;

  std::optional<long long> HeartbeatAgeSeconds(const std::string& heartbeat_at) const;
  std::optional<long long> TimestampAgeSeconds(const std::string& timestamp_text) const;
  std::string UtcNowSqlTimestamp() const;

  std::string HealthFromAge(
      const std::optional<long long>& age_seconds,
      int stale_after_seconds) const;

  std::optional<comet::RuntimeStatus> ParseRuntimeStatus(
      const comet::HostObservation& observation) const;

  std::vector<comet::RuntimeProcessStatus> ParseInstanceRuntimeStatuses(
      const comet::HostObservation& observation) const;

  std::optional<comet::GpuTelemetrySnapshot> ParseGpuTelemetry(
      const comet::HostObservation& observation) const;

  std::optional<comet::DiskTelemetrySnapshot> ParseDiskTelemetry(
      const comet::HostObservation& observation) const;

  std::optional<comet::NetworkTelemetrySnapshot> ParseNetworkTelemetry(
      const comet::HostObservation& observation) const;

  std::optional<comet::CpuTelemetrySnapshot> ParseCpuTelemetry(
      const comet::HostObservation& observation) const;

 private:
  std::time_t ToUtcTime(std::tm* timestamp) const;
};

}  // namespace comet::controller
