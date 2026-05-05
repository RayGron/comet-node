#include "telemetry/telemetry_health_builder.h"

#include <string>

namespace naim::controller {

nlohmann::json TelemetryHealthBuilder::BuildHealth(
    const std::vector<TelemetryNodeBuffer>& nodes,
    const TelemetryRetentionConfig& retention,
    const TelemetryAlertThresholds& thresholds,
    const TelemetryPersistenceState& persistence,
    const TelemetryStreamMetrics& streams,
    const std::uint64_t latest_sequence,
    const std::uint64_t dropped_frames_total,
    const std::optional<std::string>& plane_name,
    const std::uint64_t now_ms) const {
  std::vector<const TelemetryNodeBuffer*> matched_buffers;
  for (const auto& buffer : nodes) {
    if (matcher_.MatchesPlane(buffer.latest, plane_name)) {
      matched_buffers.push_back(&buffer);
    }
  }
  const auto alerts = alert_builder_.Build(
      matched_buffers,
      persistence,
      streams,
      thresholds,
      now_ms);
  std::string status = "ok";
  if (!alerts.empty()) {
    status = "degraded";
    for (const auto& alert : alerts) {
      if (alert.value("severity", std::string{}) == "critical") {
        status = "critical";
        break;
      }
    }
  }
  return nlohmann::json{
      {"schema_version", "telemetry.health.v1"},
      {"service", "naim-controller"},
      {"plane_name", plane_name.has_value() ? nlohmann::json(*plane_name) : nlohmann::json(nullptr)},
      {"status", status},
      {"observed_nodes", matched_buffers.size()},
      {"latest_sequence", latest_sequence},
      {"last_frame_age_ms",
       latest_sequence > 0 && now_ms >= latest_sequence ? now_ms - latest_sequence : 0},
      {"dropped_frames_total", dropped_frames_total},
      {"stream_batch_limit", retention.stream_batch_limit},
      {"retention",
       nlohmann::json{
           {"hot_history_capacity", retention.hot_history_capacity},
           {"durable_history_capacity", persistence.retention_capacity},
           {"stream_batch_limit", retention.stream_batch_limit},
           {"warm_bucket_ms", retention.warm_bucket_ms},
           {"cold_bucket_ms", retention.cold_bucket_ms},
       }},
      {"persistence", persistence_repository_.BuildStatus(persistence)},
      {"streams", stream_metrics_service_.BuildStatus(streams)},
      {"thresholds",
       nlohmann::json{
           {"stale_warning_ms", thresholds.stale_warning_ms},
           {"stale_critical_ms", thresholds.stale_critical_ms},
           {"ingest_warning_ms", thresholds.ingest_warning_ms},
           {"queue_warning_ms", thresholds.queue_warning_ms},
           {"browser_apply_warning_ms", thresholds.browser_apply_warning_ms},
       }},
      {"planes", plane_aggregate_builder_.Build(matched_buffers, now_ms)},
      {"alerts", alerts},
  };
}

}  // namespace naim::controller
