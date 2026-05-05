#include "telemetry/telemetry_snapshot_builder.h"

#include <algorithm>
#include <string>
#include <utility>

namespace naim::controller {

bool TelemetrySnapshotBuilder::HasActiveBackpressure(const nlohmann::json& alerts) const {
  for (const auto& alert : alerts) {
    const auto code = alert.value("code", std::string{});
    if (code == "telemetry.publisher.queue_delay" ||
        code == "telemetry.publisher.errors" ||
        code == "telemetry.stream.send_failures") {
      return true;
    }
  }
  return false;
}

std::string TelemetrySnapshotBuilder::StatusFromAlerts(
    const nlohmann::json& alerts,
    const bool overloaded) const {
  std::string status = overloaded ? "overloaded" : "ok";
  for (const auto& alert : alerts) {
    const auto severity = alert.value("severity", std::string{});
    if (severity == "critical") {
      return "critical";
    }
    if (severity == "warning") {
      status = "degraded";
    }
  }
  return status;
}

naim::HostTelemetryFrame TelemetrySnapshotBuilder::ScopeFrameToPlane(
    const naim::HostTelemetryFrame& frame,
    const std::optional<std::string>& plane_name) const {
  if (!plane_name.has_value() || plane_name->empty() || frame.plane_name == *plane_name) {
    return frame;
  }
  naim::HostTelemetryFrame scoped = frame;
  scoped.plane_name = *plane_name;
  scoped.plane_id = *plane_name;
  scoped.instance_runtime.clear();
  for (const auto& status : frame.instance_runtime) {
    if (!matcher_.RuntimeStatusMatchesPlane(status, *plane_name)) {
      continue;
    }
    auto scoped_status = status;
    if (scoped_status.plane_name.empty()) {
      scoped_status.plane_name = *plane_name;
    }
    scoped.instance_runtime.push_back(std::move(scoped_status));
  }
  scoped.plane_instance_count = scoped.instance_runtime.size();
  scoped.plane_ready_instance_count = 0;
  for (const auto& status : scoped.instance_runtime) {
    if (status.ready) {
      ++scoped.plane_ready_instance_count;
    }
  }
  scoped.plane_not_ready_instance_count =
      scoped.plane_instance_count - scoped.plane_ready_instance_count;
  if (scoped.plane_instance_count == 0) {
    scoped.plane_runtime_health = "no-runtime";
  } else {
    scoped.plane_runtime_health =
        scoped.plane_not_ready_instance_count > 0 ? "changing" : "ready";
  }
  scoped.disk.items.erase(
      std::remove_if(
          scoped.disk.items.begin(),
          scoped.disk.items.end(),
          [&](const naim::DiskTelemetryRecord& item) {
            return !item.plane_name.empty() && item.plane_name != *plane_name;
          }),
      scoped.disk.items.end());
  return scoped;
}

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
  std::vector<TelemetryNodeBuffer> scoped_buffers;
  scoped_buffers.reserve(nodes.size());
  nlohmann::json node_payloads = nlohmann::json::array();
  nlohmann::json history = nlohmann::json::array();
  for (const auto& buffer : nodes) {
    if (!matcher_.MatchesPlane(buffer.latest, plane_name)) {
      continue;
    }
    const auto scoped_latest = ScopeFrameToPlane(buffer.latest, plane_name);
    const TelemetryNodeBuffer* matched_buffer = &buffer;
    if (plane_name.has_value() && !plane_name->empty()) {
      scoped_buffers.push_back(buffer);
      scoped_buffers.back().latest = scoped_latest;
      for (auto& history_frame : scoped_buffers.back().history) {
        history_frame = ScopeFrameToPlane(history_frame, plane_name);
      }
      matched_buffer = &scoped_buffers.back();
    }
    matched_buffers.push_back(matched_buffer);
    auto node = frame_json_builder_.Build(matched_buffer->latest, now_ms);
    const auto controller_ingest_delay_ms =
        matcher_.ControllerIngestDelayMs(matched_buffer->latest, now_ms);
    node["telemetry_health"] = node_health_builder_.Build(
        matched_buffer->latest,
        *matched_buffer,
        now_ms,
        controller_ingest_delay_ms);
    node["controller_dropped_frames_total"] = matched_buffer->dropped_frames_total;
    node["controller_last_pruned_sequence"] = matched_buffer->last_pruned_sequence;
    node_payloads.push_back(std::move(node));
    if (history_seconds <= 0) {
      continue;
    }
    const std::size_t max_samples =
        static_cast<std::size_t>(std::max(1, history_seconds / 2));
    const std::size_t begin =
        matched_buffer->history.size() > max_samples
            ? matched_buffer->history.size() - max_samples
            : 0;
    for (std::size_t index = begin; index < matched_buffer->history.size(); ++index) {
      history.push_back(frame_json_builder_.Build(matched_buffer->history[index], now_ms));
    }
  }
  const auto alerts = alert_builder_.Build(
      matched_buffers,
      persistence,
      streams,
      thresholds,
      now_ms);
  const bool overloaded = HasActiveBackpressure(alerts);
  const std::string status = StatusFromAlerts(alerts, overloaded);
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
