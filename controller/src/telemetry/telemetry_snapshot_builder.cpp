#include "telemetry/telemetry_snapshot_builder.h"

#include <algorithm>
#include <string>

namespace naim::controller {

nlohmann::json TelemetrySnapshotBuilder::BuildSnapshot(
    const std::vector<TelemetryNodeBuffer>& nodes,
    const TelemetryRetentionConfig& retention,
    const TelemetryAlertThresholds& thresholds,
    const TelemetryPersistenceState& persistence,
    const TelemetryStreamMetrics& streams,
    const std::uint64_t latest_sequence,
    const std::uint64_t dropped_frames_total,
    const std::optional<std::string>& plane_name,
    const int history_seconds,
    const std::uint64_t now_ms) const {
  std::vector<const TelemetryNodeBuffer*> matched_buffers;
  nlohmann::json node_payloads = nlohmann::json::array();
  nlohmann::json history = nlohmann::json::array();
  for (const auto& buffer : nodes) {
    if (!matcher_.MatchesPlane(buffer.latest, plane_name)) {
      continue;
    }
    matched_buffers.push_back(&buffer);
    auto node = frame_json_builder_.Build(buffer.latest, now_ms);
    const auto controller_ingest_delay_ms =
        matcher_.ControllerIngestDelayMs(buffer.latest, now_ms);
    node["telemetry_health"] = node_health_builder_.Build(
        buffer.latest,
        buffer,
        now_ms,
        controller_ingest_delay_ms);
    node["controller_dropped_frames_total"] = buffer.dropped_frames_total;
    node["controller_last_pruned_sequence"] = buffer.last_pruned_sequence;
    node_payloads.push_back(std::move(node));
    if (history_seconds <= 0) {
      continue;
    }
    const std::size_t max_samples =
        static_cast<std::size_t>(std::max(1, history_seconds / 2));
    const std::size_t begin =
        buffer.history.size() > max_samples ? buffer.history.size() - max_samples : 0;
    for (std::size_t index = begin; index < buffer.history.size(); ++index) {
      history.push_back(frame_json_builder_.Build(buffer.history[index], now_ms));
    }
  }
  const bool overloaded = dropped_frames_total > 0;
  const auto alerts = alert_builder_.Build(
      matched_buffers,
      persistence,
      streams,
      thresholds,
      dropped_frames_total,
      now_ms);
  const std::string status =
      !alerts.empty() ? "degraded" : overloaded ? "overloaded" : "ok";
  return nlohmann::json{
      {"schema_version", "telemetry.snapshot.v2"},
      {"service", "naim-controller"},
      {"transport", "sse-primary"},
      {"delivery_mode", "streaming-first"},
      {"plane_name", plane_name.has_value() ? nlohmann::json(*plane_name) : nlohmann::json(nullptr)},
      {"latest_sequence", latest_sequence},
      {"telemetry_overloaded", overloaded},
      {"dropped_frames_total", dropped_frames_total},
      {"history_capacity", retention.hot_history_capacity},
      {"stream_batch_limit", retention.stream_batch_limit},
      {"persistence", persistence_repository_.BuildStatus(persistence)},
      {"streams", stream_metrics_service_.BuildStatus(streams)},
      {"alerts", alerts},
      {"retention",
       nlohmann::json{
           {"hot_history_capacity", retention.hot_history_capacity},
           {"durable_history_capacity", persistence.retention_capacity},
           {"stream_batch_limit", retention.stream_batch_limit},
           {"warm_bucket_ms", retention.warm_bucket_ms},
           {"cold_bucket_ms", retention.cold_bucket_ms},
       }},
      {"telemetry_health",
       nlohmann::json{
           {"status", status},
           {"last_frame_age_ms",
            latest_sequence > 0 && now_ms >= latest_sequence ? now_ms - latest_sequence : 0},
           {"dropped_frames_total", dropped_frames_total},
           {"stream_batch_limit", retention.stream_batch_limit},
       }},
      {"planes", plane_aggregate_builder_.Build(matched_buffers, now_ms)},
      {"nodes", std::move(node_payloads)},
      {"history", std::move(history)},
      {"downsampled_history",
       history_downsampler_.Build(
           matched_buffers,
           history_seconds,
           now_ms,
           retention.warm_bucket_ms,
           retention.cold_bucket_ms)},
  };
}

}  // namespace naim::controller
