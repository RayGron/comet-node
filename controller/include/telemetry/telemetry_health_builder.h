#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "telemetry/telemetry_frame_matcher.h"
#include "telemetry/telemetry_live_store_types.h"
#include "telemetry/telemetry_persistence_repository.h"
#include "telemetry/telemetry_stream_metrics_service.h"

namespace naim::controller {

class TelemetryHealthBuilder final {
 public:
  nlohmann::json BuildHealth(
      const std::vector<TelemetryNodeBuffer>& nodes,
      const TelemetryRetentionConfig& retention,
      const TelemetryAlertThresholds& thresholds,
      const TelemetryPersistenceState& persistence,
      const TelemetryStreamMetrics& streams,
      std::uint64_t latest_sequence,
      std::uint64_t dropped_frames_total,
      const std::optional<std::string>& plane_name,
      std::uint64_t now_ms) const;
  nlohmann::json BuildAlerts(
      const std::vector<const TelemetryNodeBuffer*>& buffers,
      const TelemetryPersistenceState& persistence,
      const TelemetryStreamMetrics& streams,
      const TelemetryAlertThresholds& thresholds,
      std::uint64_t dropped_frames_total,
      std::uint64_t now_ms) const;
  nlohmann::json BuildTelemetryHealth(
      const naim::HostTelemetryFrame& frame,
      const TelemetryNodeBuffer& buffer,
      std::uint64_t now_ms,
      std::uint64_t controller_ingest_delay_ms) const;
  nlohmann::json BuildPlaneAggregates(
      const std::vector<const TelemetryNodeBuffer*>& buffers,
      std::uint64_t now_ms) const;

 private:
  TelemetryFrameMatcher matcher_;
  TelemetryPersistenceRepository persistence_repository_;
  TelemetryStreamMetricsService stream_metrics_service_;
};

}  // namespace naim::controller
