#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "telemetry/telemetry_frame_matcher.h"
#include "telemetry/telemetry_health_builder.h"
#include "telemetry/telemetry_live_store_types.h"
#include "telemetry/telemetry_persistence_repository.h"
#include "telemetry/telemetry_stream_metrics_service.h"

namespace naim::controller {

class TelemetrySnapshotBuilder final {
 public:
  nlohmann::json BuildSnapshot(
      const std::vector<TelemetryNodeBuffer>& nodes,
      const TelemetryRetentionConfig& retention,
      const TelemetryAlertThresholds& thresholds,
      const TelemetryPersistenceState& persistence,
      const TelemetryStreamMetrics& streams,
      std::uint64_t latest_sequence,
      std::uint64_t dropped_frames_total,
      const std::optional<std::string>& plane_name,
      int history_seconds,
      std::uint64_t now_ms) const;
  nlohmann::json FrameToJson(
      const naim::HostTelemetryFrame& frame,
      std::uint64_t now_ms) const;
  nlohmann::json BuildLatencyBreakdown(
      const naim::HostTelemetryFrame& frame,
      std::uint64_t controller_ingest_delay_ms) const;
  nlohmann::json BuildDownsampledHistory(
      const std::vector<const TelemetryNodeBuffer*>& buffers,
      int history_seconds,
      std::uint64_t now_ms,
      std::uint64_t warm_bucket_ms,
      std::uint64_t cold_bucket_ms) const;

 private:
  TelemetryFrameMatcher matcher_;
  TelemetryHealthBuilder health_builder_;
  TelemetryPersistenceRepository persistence_repository_;
  TelemetryStreamMetricsService stream_metrics_service_;
};

}  // namespace naim::controller
