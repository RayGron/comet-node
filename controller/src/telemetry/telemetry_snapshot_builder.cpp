#include "telemetry/telemetry_snapshot_builder.h"

#include <algorithm>
#include <map>

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
    auto node = FrameToJson(buffer.latest, now_ms);
    const auto controller_ingest_delay_ms =
        matcher_.ControllerIngestDelayMs(buffer.latest, now_ms);
    node["telemetry_health"] = health_builder_.BuildTelemetryHealth(
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
      history.push_back(FrameToJson(buffer.history[index], now_ms));
    }
  }
  const bool overloaded = dropped_frames_total > 0;
  const auto alerts = health_builder_.BuildAlerts(
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
      {"planes", health_builder_.BuildPlaneAggregates(matched_buffers, now_ms)},
      {"nodes", std::move(node_payloads)},
      {"history", std::move(history)},
      {"downsampled_history",
       BuildDownsampledHistory(
           matched_buffers,
           history_seconds,
           now_ms,
           retention.warm_bucket_ms,
           retention.cold_bucket_ms)},
  };
}

nlohmann::json TelemetrySnapshotBuilder::FrameToJson(
    const naim::HostTelemetryFrame& frame,
    const std::uint64_t now_ms) const {
  auto payload = nlohmann::json::parse(naim::SerializeHostTelemetryFrameJson(frame));
  const std::uint64_t ttl_ms =
      frame.ttl_ms > 0 ? static_cast<std::uint64_t>(frame.ttl_ms) : 0;
  const std::uint64_t expires_at_ms = frame.sequence + ttl_ms;
  const bool stale = frame.sequence == 0 || ttl_ms == 0 || expires_at_ms <= now_ms;
  payload["stale"] = stale;
  payload["expires_in_ms"] = stale ? 0 : expires_at_ms - now_ms;
  const auto controller_ingest_delay_ms = matcher_.ControllerIngestDelayMs(frame, now_ms);
  payload["controller_ingest_delay_ms"] = controller_ingest_delay_ms;
  payload["last_frame_age_ms"] = controller_ingest_delay_ms;
  payload["telemetry_health_status"] =
      stale ? "stale"
            : frame.telemetry_dropped_frames > 0 || frame.publish_error_count > 0
                ? "degraded"
                : "ok";
  payload["latency_breakdown"] = BuildLatencyBreakdown(frame, controller_ingest_delay_ms);
  payload["transport"] = nlohmann::json{
      {"primary", "sse"},
      {"fallback", "snapshot-poll"},
      {"sequence", frame.sequence},
      {"schema_version", frame.schema_version},
  };
  return payload;
}

nlohmann::json TelemetrySnapshotBuilder::BuildLatencyBreakdown(
    const naim::HostTelemetryFrame& frame,
    const std::uint64_t controller_ingest_delay_ms) const {
  const std::uint64_t total_observed_ms =
      frame.collector_duration_ms + frame.publisher_queue_delay_ms +
      frame.publish_duration_ms + controller_ingest_delay_ms;
  return nlohmann::json{
      {"collect_ms", frame.collector_duration_ms},
      {"queue_ms", frame.publisher_queue_delay_ms},
      {"publish_ms", frame.publish_duration_ms},
      {"controller_ingest_ms", controller_ingest_delay_ms},
      {"total_observed_ms", total_observed_ms},
  };
}

nlohmann::json TelemetrySnapshotBuilder::BuildDownsampledHistory(
    const std::vector<const TelemetryNodeBuffer*>& buffers,
    const int history_seconds,
    const std::uint64_t now_ms,
    const std::uint64_t warm_bucket_ms,
    const std::uint64_t cold_bucket_ms) const {
  if (history_seconds <= 0) {
    return nlohmann::json::array();
  }
  std::map<std::string, TelemetryHistoryBucketAccumulator> buckets;
  const std::uint64_t horizon_ms =
      static_cast<std::uint64_t>(std::max(1, history_seconds)) * 1000ULL;
  for (const auto* buffer : buffers) {
    if (buffer == nullptr) {
      continue;
    }
    for (const auto& frame : buffer->history) {
      if (frame.sequence == 0 || now_ms > frame.sequence + horizon_ms) {
        continue;
      }
      const std::uint64_t age_ms = now_ms >= frame.sequence ? now_ms - frame.sequence : 0;
      const std::uint64_t bucket_ms =
          age_ms <= warm_bucket_ms ? warm_bucket_ms : cold_bucket_ms;
      const std::uint64_t bucket_start_ms = (frame.sequence / bucket_ms) * bucket_ms;
      const auto key =
          frame.node_name + "|" + matcher_.PlaneKeyForFrame(frame) + "|" +
          std::to_string(bucket_ms) + "|" + std::to_string(bucket_start_ms);
      auto& bucket = buckets[key];
      if (bucket.sample_count == 0) {
        bucket.node_name = frame.node_name;
        bucket.plane_name = matcher_.PlaneKeyForFrame(frame);
        bucket.bucket_start_ms = bucket_start_ms;
        bucket.bucket_ms = bucket_ms;
        bucket.first_sequence = frame.sequence;
      }
      bucket.last_sequence = std::max(bucket.last_sequence, frame.sequence);
      bucket.sample_count += 1;
      bucket.cpu_utilization_sum += frame.cpu.utilization_pct;
      const double gpu_util = matcher_.GpuUtilizationAverage(frame);
      bucket.gpu_utilization_sum += gpu_util;
      bucket.max_gpu_utilization_pct = std::max(bucket.max_gpu_utilization_pct, gpu_util);
      bucket.max_queue_delay_ms =
          std::max(bucket.max_queue_delay_ms, frame.publisher_queue_delay_ms);
      bucket.max_publish_ms = std::max(bucket.max_publish_ms, frame.publish_duration_ms);
      bucket.max_controller_ingest_ms = std::max(
          bucket.max_controller_ingest_ms,
          matcher_.ControllerIngestDelayMs(frame, now_ms));
    }
  }
  nlohmann::json result = nlohmann::json::array();
  for (const auto& [_, bucket] : buckets) {
    const double count = static_cast<double>(std::max<std::uint64_t>(1, bucket.sample_count));
    result.push_back(nlohmann::json{
        {"node_name", bucket.node_name},
        {"plane_name", bucket.plane_name},
        {"bucket_start_ms", bucket.bucket_start_ms},
        {"bucket_ms", bucket.bucket_ms},
        {"sample_count", bucket.sample_count},
        {"first_sequence", bucket.first_sequence},
        {"last_sequence", bucket.last_sequence},
        {"avg_cpu_utilization_pct", bucket.cpu_utilization_sum / count},
        {"avg_gpu_utilization_pct", bucket.gpu_utilization_sum / count},
        {"max_gpu_utilization_pct", bucket.max_gpu_utilization_pct},
        {"max_queue_delay_ms", bucket.max_queue_delay_ms},
        {"max_publish_ms", bucket.max_publish_ms},
        {"max_controller_ingest_ms", bucket.max_controller_ingest_ms},
    });
  }
  return result;
}

}  // namespace naim::controller
