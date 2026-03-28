#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "comet/runtime/runtime_status.h"
#include "comet/state/sqlite_store.h"

namespace comet::controller {

class ReadModelService {
 public:
  using HeartbeatAgeSecondsFn =
      std::function<std::optional<long long>(const std::string&)>;
  using HealthFromAgeFn =
      std::function<std::string(const std::optional<long long>&, int)>;
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
  using ObservationMatchesPlaneFn =
      std::function<bool(const comet::HostObservation&, const std::string&)>;
  using ResolveNodeAvailabilityFn =
      std::function<comet::NodeAvailability(
          const std::map<std::string, comet::NodeAvailabilityOverride>&,
          const std::string&)>;
  using BuildAvailabilityOverrideMapFn =
      std::function<std::map<std::string, comet::NodeAvailabilityOverride>(
          const std::vector<comet::NodeAvailabilityOverride>&)>;

  struct Deps {
    HeartbeatAgeSecondsFn heartbeat_age_seconds;
    HealthFromAgeFn health_from_age;
    ParseRuntimeStatusFn parse_runtime_status;
    ParseInstanceRuntimeStatusesFn parse_instance_runtime_statuses;
    ParseGpuTelemetryFn parse_gpu_telemetry;
    ParseDiskTelemetryFn parse_disk_telemetry;
    ParseNetworkTelemetryFn parse_network_telemetry;
    ParseCpuTelemetryFn parse_cpu_telemetry;
    ObservationMatchesPlaneFn observation_matches_plane;
    ResolveNodeAvailabilityFn resolve_node_availability;
    BuildAvailabilityOverrideMapFn build_availability_override_map;
  };

  explicit ReadModelService(Deps deps);

  nlohmann::json BuildEventPayloadItem(
      const comet::EventRecord& event) const;

  nlohmann::json BuildHostAssignmentsPayload(
      const std::string& db_path,
      const std::optional<std::string>& node_name) const;

  nlohmann::json BuildHostObservationsPayload(
      const std::string& db_path,
      const std::optional<std::string>& node_name,
      const std::optional<std::string>& plane_name,
      int stale_after_seconds) const;

  nlohmann::json BuildHostHealthPayload(
      const std::string& db_path,
      const std::optional<std::string>& node_name,
      int stale_after_seconds) const;

  nlohmann::json BuildDiskStatePayload(
      const std::string& db_path,
      const std::optional<std::string>& node_name,
      const std::optional<std::string>& plane_name) const;

  nlohmann::json BuildEventsPayload(
      const std::string& db_path,
      const std::optional<std::string>& plane_name,
      const std::optional<std::string>& node_name,
      const std::optional<std::string>& worker_name,
      const std::optional<std::string>& category,
      int limit) const;

  nlohmann::json BuildNodeAvailabilityPayload(
      const std::string& db_path,
      const std::optional<std::string>& node_name) const;

 private:
  Deps deps_;
};

}  // namespace comet::controller
