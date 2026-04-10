#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "comet/runtime/runtime_status.h"
#include "comet/state/sqlite_store.h"

namespace comet::controller {

class PlaneObservationMatcher {
 public:
  bool MatchesPlaneInstanceName(
      const std::string& instance_name,
      const std::string& plane_name,
      const std::set<std::string>& plane_instance_names = {}) const;

  std::set<std::string> CollectPlaneInstanceNames(
      const comet::DesiredState& observed_state,
      const std::string& plane_name) const;

  comet::DesiredState FilterObservedStateForPlane(
      const comet::DesiredState& observed_state,
      const std::string& plane_name) const;

  std::optional<comet::DesiredState> ParseObservedStateForPlane(
      const comet::HostObservation& observation,
      const std::optional<std::string>& plane_name) const;

  bool ObservationMatchesPlane(
      const comet::HostObservation& observation,
      const std::string& plane_name) const;

  std::vector<comet::HostObservation> FilterHostObservationsForPlane(
      const std::vector<comet::HostObservation>& observations,
      const std::string& plane_name) const;

  std::optional<comet::RuntimeStatus> FilterRuntimeStatusForPlane(
      const std::optional<comet::RuntimeStatus>& runtime_status,
      const std::optional<std::string>& plane_name,
      const std::set<std::string>& plane_instance_names) const;

  std::vector<comet::RuntimeProcessStatus> FilterInstanceRuntimeStatusesForPlane(
      const std::vector<comet::RuntimeProcessStatus>& statuses,
      const std::optional<std::string>& plane_name,
      const std::set<std::string>& plane_instance_names) const;

  std::optional<comet::GpuTelemetrySnapshot> FilterGpuTelemetryForPlane(
      const std::optional<comet::GpuTelemetrySnapshot>& snapshot,
      const std::optional<std::string>& plane_name,
      const std::set<std::string>& plane_instance_names) const;

  std::optional<comet::DiskTelemetrySnapshot> FilterDiskTelemetryForPlane(
      const std::optional<comet::DiskTelemetrySnapshot>& snapshot,
      const std::optional<std::string>& plane_name) const;

 private:
  bool HasPlaneEntities(const comet::DesiredState& observed_state) const;

  const std::vector<std::string>& RuntimeInstancePrefixes() const;
};

}  // namespace comet::controller
