#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "telemetry/telemetry_alert_builder.h"
#include "telemetry/telemetry_frame_matcher.h"
#include "telemetry/telemetry_frame_json_builder.h"
#include "telemetry/telemetry_history_downsampler.h"
#include "telemetry/telemetry_live_store_types.h"
#include "telemetry/telemetry_node_health_builder.h"
#include "telemetry/telemetry_plane_aggregate_builder.h"
#include "telemetry/telemetry_persistence_repository.h"
#include "telemetry/telemetry_state_types.h"
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

 private:
  bool HasActiveBackpressure(const nlohmann::json& alerts) const;
  std::string StatusFromAlerts(const nlohmann::json& alerts, bool overloaded) const;
  naim::HostTelemetryFrame ScopeFrameToPlane(
      const naim::HostTelemetryFrame& frame,
      const std::optional<std::string>& plane_name) const;

  TelemetryAlertBuilder alert_builder_;
  TelemetryFrameMatcher matcher_;
  TelemetryFrameJsonBuilder frame_json_builder_;
  TelemetryHistoryDownsampler history_downsampler_;
  TelemetryNodeHealthBuilder node_health_builder_;
  TelemetryPlaneAggregateBuilder plane_aggregate_builder_;
  TelemetryPersistenceRepository persistence_repository_;
  TelemetryStreamMetricsService stream_metrics_service_;
};

}  // namespace naim::controller
