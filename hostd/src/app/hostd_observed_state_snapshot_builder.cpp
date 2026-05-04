#include "app/hostd_observed_state_snapshot_builder.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

#include "naim/state/state_json.h"

namespace naim::hostd {

namespace {

std::string TimestampString(std::chrono::system_clock::time_point time_point) {
  const std::time_t now = std::chrono::system_clock::to_time_t(time_point);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &now);
#else
  localtime_r(&now, &tm);
#endif
  char buffer[32];
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm) == 0) {
    return {};
  }
  return buffer;
}

std::uint64_t CurrentSequenceMillis() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

std::uint64_t CurrentMonotonicMillis() {
  static const auto started_at = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::steady_clock::now() - started_at;
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

std::string JoinReasons(const std::vector<std::string>& reasons) {
  std::string joined;
  for (const auto& reason : reasons) {
    if (reason.empty()) {
      continue;
    }
    if (!joined.empty()) {
      joined += ", ";
    }
    joined += reason;
  }
  return joined;
}

}  // namespace

HostdObservedStateSnapshotBuilder::HostdObservedStateSnapshotBuilder(
    const HostdLocalStateRepository& local_state_repository,
    const HostdLocalRuntimeStateSupport& local_runtime_state_support,
    const HostdRuntimeTelemetrySupport& runtime_telemetry_support,
    const HostdSystemTelemetryCollector& system_telemetry_collector)
    : local_state_repository_(local_state_repository),
      local_runtime_state_support_(local_runtime_state_support),
      runtime_telemetry_support_(runtime_telemetry_support),
      system_telemetry_collector_(system_telemetry_collector) {}

naim::HostObservation HostdObservedStateSnapshotBuilder::BuildObservedStateSnapshot(
    const std::string& node_name,
    const std::string& storage_root,
    const std::string& state_root,
    naim::HostObservationStatus status,
    const std::string& status_message,
    const std::optional<int>& assignment_id) const {
  naim::HostObservation observation;
  observation.node_name = node_name;
  observation.status = status;
  observation.status_message = status_message;
  observation.last_assignment_id = assignment_id;
  observation.applied_generation =
      local_state_repository_.LoadLocalAppliedGeneration(state_root, node_name);

  const auto local_state = local_state_repository_.LoadLocalAppliedState(state_root, node_name);
  if (local_state.has_value()) {
    observation.plane_name = local_state->plane_name;
    observation.observed_state_json = naim::SerializeDesiredStateJson(*local_state);
  }
  const auto runtime_status =
      local_runtime_state_support_.LoadLocalRuntimeStatus(state_root, node_name);
  if (runtime_status.has_value()) {
    observation.runtime_status_json = naim::SerializeRuntimeStatusJson(*runtime_status);
  }
  auto instance_statuses =
      runtime_telemetry_support_.LoadLocalInstanceRuntimeStatuses(state_root, node_name);
  runtime_telemetry_support_.ResolveInstanceHostPids(&instance_statuses);
  if (!instance_statuses.empty()) {
    observation.instance_runtime_json = naim::SerializeRuntimeStatusListJson(instance_statuses);
  }
  const naim::DesiredState telemetry_state =
      local_state.has_value() ? *local_state : naim::DesiredState{};
  observation.gpu_telemetry_json = naim::SerializeGpuTelemetryJson(
      system_telemetry_collector_.CollectGpuTelemetry(
          telemetry_state,
          node_name,
          instance_statuses));
  naim::DiskTelemetrySnapshot disk_snapshot;
  disk_snapshot.contract_version = 1;
  disk_snapshot.source = "hostd";
  disk_snapshot.collected_at = "";
  disk_snapshot.items.push_back(
      system_telemetry_collector_.BuildStorageRootTelemetry(node_name, storage_root));
  if (local_state.has_value()) {
    const auto managed_snapshot =
        system_telemetry_collector_.CollectDiskTelemetry(*local_state, node_name);
    disk_snapshot.degraded = managed_snapshot.degraded;
    if (!managed_snapshot.source.empty()) {
      disk_snapshot.source = managed_snapshot.source;
    }
    if (!managed_snapshot.collected_at.empty()) {
      disk_snapshot.collected_at = managed_snapshot.collected_at;
    }
    disk_snapshot.items.insert(
        disk_snapshot.items.end(),
        managed_snapshot.items.begin(),
        managed_snapshot.items.end());
  }
  observation.disk_telemetry_json = naim::SerializeDiskTelemetryJson(disk_snapshot);
  observation.network_telemetry_json = naim::SerializeNetworkTelemetryJson(
      system_telemetry_collector_.CollectNetworkTelemetry(state_root));
  observation.cpu_telemetry_json = naim::SerializeCpuTelemetryJson(
      system_telemetry_collector_.CollectCpuTelemetry());

  return observation;
}

naim::HostTelemetryFrame HostdObservedStateSnapshotBuilder::BuildTelemetryFrame(
    const std::string& node_name,
    const std::string& storage_root,
    const std::string& state_root,
    const int interval_ms,
    const int ttl_ms,
    const bool include_slow_lane) const {
  naim::HostTelemetryFrame frame;
  frame.node_name = node_name;
  const auto sampled_at = std::chrono::system_clock::now();
  frame.sampled_at = TimestampString(sampled_at);
  frame.collected_at = frame.sampled_at;
  frame.expires_at =
      TimestampString(sampled_at + std::chrono::milliseconds(std::max(1, ttl_ms)));
  frame.sequence = CurrentSequenceMillis();
  frame.monotonic_ms = CurrentMonotonicMillis();
  frame.interval_ms = interval_ms;
  frame.ttl_ms = ttl_ms;
  frame.lane = include_slow_lane ? "full" : "fast";

  const auto local_state = local_state_repository_.LoadLocalAppliedState(state_root, node_name);
  if (local_state.has_value()) {
    frame.plane_name = local_state->plane_name;
  }
  const naim::DesiredState telemetry_state =
      local_state.has_value() ? *local_state : naim::DesiredState{};
  frame.instance_runtime =
      runtime_telemetry_support_.LoadLocalInstanceRuntimeStatuses(state_root, node_name);
  runtime_telemetry_support_.ResolveInstanceHostPids(&frame.instance_runtime);
  frame.gpu = system_telemetry_collector_.CollectGpuTelemetry(
      telemetry_state,
      node_name,
      frame.instance_runtime);
  frame.network = system_telemetry_collector_.CollectNetworkTelemetry(state_root);
  frame.cpu = system_telemetry_collector_.CollectCpuTelemetry();
  if (include_slow_lane) {
    frame.disk.contract_version = 1;
    frame.disk.source = "hostd";
    frame.disk.collected_at = frame.sampled_at;
    frame.disk.items.push_back(
        system_telemetry_collector_.BuildStorageRootTelemetry(node_name, storage_root));
    if (local_state.has_value()) {
      const auto managed_snapshot =
          system_telemetry_collector_.CollectDiskTelemetry(*local_state, node_name);
      frame.disk.degraded = managed_snapshot.degraded;
      if (!managed_snapshot.source.empty()) {
        frame.disk.source = managed_snapshot.source;
      }
      if (!managed_snapshot.collected_at.empty()) {
        frame.disk.collected_at = managed_snapshot.collected_at;
      }
      frame.disk.items.insert(
          frame.disk.items.end(),
          managed_snapshot.items.begin(),
          managed_snapshot.items.end());
    }
  }
  std::vector<std::string> degraded_reasons;
  if (frame.cpu.degraded) {
    degraded_reasons.push_back(frame.cpu.source.empty() ? "cpu" : "cpu:" + frame.cpu.source);
  }
  if (frame.gpu.degraded) {
    degraded_reasons.push_back(frame.gpu.source.empty() ? "gpu" : "gpu:" + frame.gpu.source);
  }
  if (frame.network.degraded) {
    degraded_reasons.push_back(
        frame.network.source.empty() ? "network" : "network:" + frame.network.source);
  }
  if (frame.disk.degraded) {
    degraded_reasons.push_back(
        frame.disk.source.empty() ? "disk" : "disk:" + frame.disk.source);
  }
  frame.degraded_reason = JoinReasons(degraded_reasons);
  return frame;
}

}  // namespace naim::hostd
