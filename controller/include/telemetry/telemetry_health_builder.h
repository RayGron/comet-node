#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "telemetry/telemetry_alert_builder.h"
#include "telemetry/telemetry_frame_matcher.h"
#include "telemetry/telemetry_live_store_types.h"
#include "telemetry/telemetry_plane_aggregate_builder.h"
#include "telemetry/telemetry_persistence_repository.h"
#include "telemetry/telemetry_state_types.h"
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

 private:
  TelemetryFrameMatcher matcher_;
  TelemetryAlertBuilder alert_builder_;
  TelemetryPersistenceRepository persistence_repository_;
  TelemetryPlaneAggregateBuilder plane_aggregate_builder_;
  TelemetryStreamMetricsService stream_metrics_service_;
};

}  // namespace naim::controller
