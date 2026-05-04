#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "naim/runtime/runtime_status.h"
#include "telemetry/telemetry_clock.h"
#include "telemetry/telemetry_frame_normalizer.h"
#include "telemetry/telemetry_frame_ring_buffer.h"
#include "telemetry/telemetry_health_builder.h"
#include "telemetry/telemetry_live_store_types.h"
#include "telemetry/telemetry_open_metrics_exporter.h"
#include "telemetry/telemetry_operational_config.h"
#include "telemetry/telemetry_persistence_repository.h"
#include "telemetry/telemetry_snapshot_builder.h"
#include "telemetry/telemetry_stream_metrics_service.h"

namespace naim::controller {

class TelemetryLiveStore final {
 public:
  using RetentionConfig = TelemetryRetentionConfig;
  using AlertThresholds = TelemetryAlertThresholds;
  using StreamDelta = TelemetryStreamDelta;

  static TelemetryLiveStore& Instance();

  void ConfigurePersistence(
      const std::string& db_path,
      std::size_t retention_capacity = 9600);
  void ConfigureFromEnvironment(const std::string& db_path);
  void ConfigureOperationalPolicy(
      RetentionConfig retention,
      AlertThresholds thresholds);
  void ResetForTests();

  bool Upsert(naim::HostTelemetryFrame frame);
  nlohmann::json BuildSnapshot(
      const std::optional<std::string>& plane_name,
      int history_seconds) const;
  nlohmann::json BuildHealth(
      const std::optional<std::string>& plane_name) const;
  std::string BuildOpenMetrics(
      const std::optional<std::string>& plane_name) const;
  std::vector<naim::HostTelemetryFrame> LoadFramesAfter(
      std::uint64_t sequence,
      const std::optional<std::string>& plane_name) const;
  StreamDelta LoadStreamDeltaAfter(
      std::uint64_t sequence,
      const std::optional<std::string>& plane_name) const;
  std::uint64_t LatestSequence() const;
  void RecordStreamOpened(const std::string& stream_name);
  void RecordStreamClosed(const std::string& stream_name);
  void RecordStreamReplayRequired(const std::string& stream_name);
  void RecordStreamSendFailure(const std::string& stream_name);

 private:
  bool UpsertInMemoryLocked(naim::HostTelemetryFrame frame);
  void ConfigurePersistenceLocked(
      const std::string& db_path,
      std::size_t retention_capacity);

  mutable std::mutex mutex_;
  std::vector<TelemetryNodeBuffer> nodes_;
  std::uint64_t latest_sequence_ = 0;
  std::uint64_t dropped_frames_total_ = 0;
  RetentionConfig retention_;
  AlertThresholds thresholds_;
  TelemetryPersistenceState persistence_;
  TelemetryStreamMetrics streams_;
  TelemetryClock clock_;
  TelemetryFrameNormalizer frame_normalizer_;
  TelemetryOperationalConfig operational_config_;
  TelemetryFrameRingBuffer ring_buffer_;
  TelemetryPersistenceRepository persistence_repository_;
  TelemetryStreamMetricsService stream_metrics_service_;
  TelemetryHealthBuilder health_builder_;
  TelemetrySnapshotBuilder snapshot_builder_;
  TelemetryOpenMetricsExporter open_metrics_exporter_;
};

}  // namespace naim::controller
