#pragma once

#include <optional>
#include <string>

#include "app/hostd_desired_state_path_support.h"
#include "app/hostd_local_runtime_state_support.h"
#include "app/hostd_local_state_path_support.h"
#include "app/hostd_local_state_repository.h"
#include "app/hostd_runtime_telemetry_support.h"
#include "app/hostd_system_telemetry_collector.h"
#include "comet/state/sqlite_store.h"

namespace comet::hostd {

class HostdObservedStateSnapshotBuilder final {
 public:
  HostdObservedStateSnapshotBuilder(
      const HostdLocalStateRepository& local_state_repository,
      const HostdLocalRuntimeStateSupport& local_runtime_state_support,
      const HostdRuntimeTelemetrySupport& runtime_telemetry_support,
      const HostdSystemTelemetryCollector& system_telemetry_collector);

  comet::HostObservation BuildObservedStateSnapshot(
      const std::string& node_name,
      const std::string& storage_root,
      const std::string& state_root,
      comet::HostObservationStatus status,
      const std::string& status_message,
      const std::optional<int>& assignment_id = std::nullopt) const;

 private:
  const HostdLocalStateRepository& local_state_repository_;
  const HostdLocalRuntimeStateSupport& local_runtime_state_support_;
  const HostdRuntimeTelemetrySupport& runtime_telemetry_support_;
  const HostdSystemTelemetryCollector& system_telemetry_collector_;
};

}  // namespace comet::hostd
