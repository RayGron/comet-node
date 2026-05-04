#include "observation/hostd_observation_service.h"

#include <chrono>
#include <algorithm>
#include <iostream>
#include <thread>

namespace naim::hostd {

HostdObservationService::HostdObservationService(
    const IHostdBackendFactory& backend_factory,
    const IHostdObservationSupport& support)
    : backend_factory_(backend_factory),
      support_(support) {}

void HostdObservationService::ShowLocalState(
    const std::string& node_name,
    const std::string& state_root) const {
  support_.ShowLocalState(node_name, state_root);
}

void HostdObservationService::ShowRuntimeStatus(
    const std::string& node_name,
    const std::string& state_root) const {
  support_.ShowRuntimeStatus(node_name, state_root);
}

void HostdObservationService::ReportObservedState(
    HostdBackend& backend,
    const naim::HostObservation& observation,
    const std::string& source_label) const {
  backend.UpsertHostObservation(observation);
  support_.AppendHostdEvent(
      backend,
      "host-observation",
      "reported",
      source_label,
      nlohmann::json{
          {"status", naim::ToString(observation.status)},
          {"applied_generation",
           observation.applied_generation.has_value() ? nlohmann::json(*observation.applied_generation)
                                                      : nlohmann::json(nullptr)},
          {"last_assignment_id",
           observation.last_assignment_id.has_value() ? nlohmann::json(*observation.last_assignment_id)
                                                      : nlohmann::json(nullptr)},
      },
      observation.plane_name,
      observation.node_name,
      "",
      observation.last_assignment_id,
      std::nullopt,
      "info");

  std::cout << source_label << "\n";
  std::cout << "backend=hostd-control\n";
  std::cout << "node=" << observation.node_name << "\n";
  std::cout << "status=" << naim::ToString(observation.status) << "\n";
  if (!observation.plane_name.empty()) {
    std::cout << "plane=" << observation.plane_name << "\n";
  }
  if (observation.applied_generation.has_value()) {
    std::cout << "applied_generation=" << *observation.applied_generation << "\n";
  }
  if (observation.last_assignment_id.has_value()) {
    std::cout << "last_assignment_id=" << *observation.last_assignment_id << "\n";
  }
  if (!observation.status_message.empty()) {
    std::cout << "message=" << observation.status_message << "\n";
  }
}

void HostdObservationService::ReportLocalObservedState(
    const std::optional<std::string>& db_path,
    const std::optional<std::string>& controller_url,
    const std::optional<std::string>& host_private_key_path,
    const std::optional<std::string>& controller_fingerprint,
    const std::optional<std::string>& onboarding_key,
    const std::string& node_name,
    const std::string& storage_root,
    const std::string& state_root) const {
  auto backend = backend_factory_.CreateBackend(
      db_path,
      controller_url,
      host_private_key_path,
      controller_fingerprint,
      onboarding_key,
      node_name,
      storage_root);
  ReportObservedState(
      *backend,
      support_.BuildObservedStateSnapshot(
          node_name,
          storage_root,
          state_root,
          naim::HostObservationStatus::Idle,
          "manual heartbeat"),
      "hostd report-observed-state");
}

void HostdObservationService::ReportLocalTelemetry(
    const std::optional<std::string>& db_path,
    const std::optional<std::string>& controller_url,
    const std::optional<std::string>& host_private_key_path,
    const std::optional<std::string>& controller_fingerprint,
    const std::optional<std::string>& onboarding_key,
    const std::string& node_name,
    const std::string& storage_root,
    const std::string& state_root,
    const int interval_ms,
    const int ttl_ms) const {
  auto backend = backend_factory_.CreateBackend(
      db_path,
      controller_url,
      host_private_key_path,
      controller_fingerprint,
      onboarding_key,
      node_name,
      storage_root);
  const auto frame = support_.BuildTelemetryFrame(
      node_name,
      storage_root,
      state_root,
      interval_ms,
      ttl_ms,
      true);
  backend->UpsertHostTelemetry(frame);
  std::cout << "hostd report-telemetry\n";
  std::cout << "backend=hostd-control\n";
  std::cout << "node=" << frame.node_name << "\n";
  if (!frame.plane_name.empty()) {
    std::cout << "plane=" << frame.plane_name << "\n";
  }
  std::cout << "sequence=" << frame.sequence << "\n";
}

void HostdObservationService::WatchLocalTelemetry(
    const std::optional<std::string>& db_path,
    const std::optional<std::string>& controller_url,
    const std::optional<std::string>& host_private_key_path,
    const std::optional<std::string>& controller_fingerprint,
    const std::optional<std::string>& onboarding_key,
    const std::string& node_name,
    const std::string& storage_root,
    const std::string& state_root,
    const int interval_ms,
    const int ttl_ms) const {
  auto backend = backend_factory_.CreateBackend(
      db_path,
      controller_url,
      host_private_key_path,
      controller_fingerprint,
      onboarding_key,
      node_name,
      storage_root);
  const auto interval = std::chrono::milliseconds(std::max(250, interval_ms));
  const auto slow_interval = std::max(
      interval * 5,
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::seconds(10)));
  std::cout << "hostd report-telemetry watch\n";
  std::cout << "mode=diagnostic\n";
  std::cout << "backend=hostd-control\n";
  std::cout << "node=" << node_name << "\n";
  std::cout << "interval_ms=" << interval.count() << "\n";
  std::cout << "slow_interval_ms=" << slow_interval.count() << "\n";
  std::cout.flush();

  auto next_tick = std::chrono::steady_clock::now();
  auto next_slow_tick = next_tick;
  while (true) {
    next_tick += interval;
    try {
      const auto now = std::chrono::steady_clock::now();
      const bool include_slow_lane = now >= next_slow_tick;
      if (include_slow_lane) {
        next_slow_tick = now + slow_interval;
      }
      const auto frame = support_.BuildTelemetryFrame(
          node_name,
          storage_root,
          state_root,
          static_cast<int>(interval.count()),
          ttl_ms,
          include_slow_lane);
      backend->UpsertHostTelemetry(frame);
    } catch (const std::exception& error) {
      std::cerr << "hostd report-telemetry warning: " << error.what() << "\n";
    }
    std::this_thread::sleep_until(next_tick);
    if (std::chrono::steady_clock::now() > next_tick + interval) {
      next_tick = std::chrono::steady_clock::now();
    }
  }
}

}  // namespace naim::hostd
