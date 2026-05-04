#include "telemetry/telemetry_health_builder.h"

#include <algorithm>
#include <map>

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
  const auto alerts = BuildAlerts(
      matched_buffers,
      persistence,
      streams,
      thresholds,
      dropped_frames_total,
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
      {"planes", BuildPlaneAggregates(matched_buffers, now_ms)},
      {"alerts", alerts},
  };
}

nlohmann::json TelemetryHealthBuilder::BuildAlerts(
    const std::vector<const TelemetryNodeBuffer*>& buffers,
    const TelemetryPersistenceState& persistence,
    const TelemetryStreamMetrics& streams,
    const TelemetryAlertThresholds& thresholds,
    const std::uint64_t dropped_frames_total,
    const std::uint64_t now_ms) const {
  nlohmann::json alerts = nlohmann::json::array();
  if (!persistence.enabled) {
    alerts.push_back(nlohmann::json{
        {"code", "telemetry.persistence.disabled"},
        {"severity", "warning"},
        {"message", "sqlite telemetry ring buffer is disabled"},
    });
  }
  if (persistence.error_count > 0) {
    alerts.push_back(nlohmann::json{
        {"code", "telemetry.persistence.errors"},
        {"severity", "warning"},
        {"message", "sqlite telemetry ring buffer reported errors"},
        {"count", persistence.error_count},
        {"last_error", persistence.last_error},
    });
  }
  if (dropped_frames_total > 0) {
    alerts.push_back(nlohmann::json{
        {"code", "telemetry.controller.dropped_frames"},
        {"severity", "warning"},
        {"message", "controller telemetry ring buffer pruned live frames"},
        {"count", dropped_frames_total},
    });
  }
  if (streams.telemetry.send_failure_total > 0 || streams.live.send_failure_total > 0) {
    alerts.push_back(nlohmann::json{
        {"code", "telemetry.stream.send_failures"},
        {"severity", "warning"},
        {"message", "telemetry stream clients observed send failures"},
        {"telemetry_failures", streams.telemetry.send_failure_total},
        {"live_failures", streams.live.send_failure_total},
    });
  }
  if (streams.telemetry.replay_required_total > 0 || streams.live.replay_required_total > 0) {
    alerts.push_back(nlohmann::json{
        {"code", "telemetry.stream.replay_required"},
        {"severity", "warning"},
        {"message", "telemetry stream replay was required"},
        {"telemetry_replay_required_total", streams.telemetry.replay_required_total},
        {"live_replay_required_total", streams.live.replay_required_total},
    });
  }
  for (const auto* buffer : buffers) {
    if (buffer == nullptr) {
      continue;
    }
    const auto& frame = buffer->latest;
    const auto ingest_ms = matcher_.ControllerIngestDelayMs(frame, now_ms);
    if (ingest_ms >= thresholds.stale_critical_ms) {
      alerts.push_back(nlohmann::json{
          {"code", "telemetry.node.stale"},
          {"severity", "critical"},
          {"node_name", frame.node_name},
          {"plane_name", matcher_.PlaneKeyForFrame(frame)},
          {"age_ms", ingest_ms},
      });
    } else if (ingest_ms >= thresholds.stale_warning_ms) {
      alerts.push_back(nlohmann::json{
          {"code", "telemetry.node.slow"},
          {"severity", "warning"},
          {"node_name", frame.node_name},
          {"plane_name", matcher_.PlaneKeyForFrame(frame)},
          {"age_ms", ingest_ms},
      });
    }
    if (ingest_ms >= thresholds.ingest_warning_ms) {
      alerts.push_back(nlohmann::json{
          {"code", "telemetry.controller.ingest_delay"},
          {"severity", "warning"},
          {"node_name", frame.node_name},
          {"plane_name", matcher_.PlaneKeyForFrame(frame)},
          {"delay_ms", ingest_ms},
      });
    }
    if (frame.publisher_queue_delay_ms >= thresholds.queue_warning_ms) {
      alerts.push_back(nlohmann::json{
          {"code", "telemetry.publisher.queue_delay"},
          {"severity", "warning"},
          {"node_name", frame.node_name},
          {"plane_name", matcher_.PlaneKeyForFrame(frame)},
          {"delay_ms", frame.publisher_queue_delay_ms},
      });
    }
    if (frame.publish_error_count > 0) {
      alerts.push_back(nlohmann::json{
          {"code", "telemetry.publisher.errors"},
          {"severity", "warning"},
          {"node_name", frame.node_name},
          {"plane_name", matcher_.PlaneKeyForFrame(frame)},
          {"count", frame.publish_error_count},
          {"last_error", frame.last_publish_error},
      });
    }
  }
  return alerts;
}

nlohmann::json TelemetryHealthBuilder::BuildTelemetryHealth(
    const naim::HostTelemetryFrame& frame,
    const TelemetryNodeBuffer& buffer,
    const std::uint64_t now_ms,
    const std::uint64_t controller_ingest_delay_ms) const {
  const std::uint64_t ttl_ms =
      frame.ttl_ms > 0 ? static_cast<std::uint64_t>(frame.ttl_ms) : 0;
  const bool stale =
      frame.sequence == 0 || ttl_ms == 0 || frame.sequence + ttl_ms <= now_ms;
  std::string status = "ok";
  if (stale) {
    status = "stale";
  } else if (
      buffer.dropped_frames_total > 0 || frame.telemetry_dropped_frames > 0 ||
      frame.publish_error_count > 0 || !frame.degraded_reason.empty()) {
    status = "degraded";
  }
  return nlohmann::json{
      {"status", status},
      {"last_frame_age_ms", controller_ingest_delay_ms},
      {"dropped_frames_total", buffer.dropped_frames_total},
      {"publish_error_count", frame.publish_error_count},
      {"publish_error", frame.last_publish_error},
      {"degraded_reason", frame.degraded_reason},
  };
}

nlohmann::json TelemetryHealthBuilder::BuildPlaneAggregates(
    const std::vector<const TelemetryNodeBuffer*>& buffers,
    const std::uint64_t now_ms) const {
  std::map<std::string, TelemetryPlaneAccumulator> planes;
  for (const auto* buffer : buffers) {
    if (buffer == nullptr) {
      continue;
    }
    const auto& frame = buffer->latest;
    auto& plane = planes[matcher_.PlaneKeyForFrame(frame)];
    if (plane.plane_name.empty()) {
      plane.plane_name = matcher_.PlaneKeyForFrame(frame);
    }
    const auto ingest_ms = matcher_.ControllerIngestDelayMs(frame, now_ms);
    const auto health = BuildTelemetryHealth(frame, *buffer, now_ms, ingest_ms);
    plane.node_count += 1;
    plane.latest_sequence = std::max(plane.latest_sequence, frame.sequence);
    plane.dropped_frames_total += buffer->dropped_frames_total;
    plane.max_last_frame_age_ms = std::max(plane.max_last_frame_age_ms, ingest_ms);
    plane.max_queue_delay_ms = std::max(plane.max_queue_delay_ms, frame.publisher_queue_delay_ms);
    plane.max_publish_ms = std::max(plane.max_publish_ms, frame.publish_duration_ms);
    plane.max_controller_ingest_ms = std::max(plane.max_controller_ingest_ms, ingest_ms);
    plane.gpu_count += frame.gpu.devices.size();
    plane.plane_instance_count += frame.plane_instance_count;
    plane.plane_ready_instance_count += frame.plane_ready_instance_count;
    plane.plane_not_ready_instance_count += frame.plane_not_ready_instance_count;
    plane.gpu_utilization_sum += matcher_.GpuUtilizationAverage(frame);
    if (health.value("status", std::string{"ok"}) == "stale") {
      plane.stale_nodes += 1;
    } else if (health.value("status", std::string{"ok"}) == "degraded") {
      plane.degraded_nodes += 1;
    }
  }
  nlohmann::json result = nlohmann::json::array();
  for (const auto& [_, plane] : planes) {
    const bool overloaded = plane.dropped_frames_total > 0;
    std::string status = "ok";
    if (plane.stale_nodes > 0) {
      status = "stale";
    } else if (plane.degraded_nodes > 0 || overloaded) {
      status = "degraded";
    }
    const double node_count = static_cast<double>(std::max<std::uint64_t>(1, plane.node_count));
    result.push_back(nlohmann::json{
        {"plane_name", plane.plane_name},
        {"status", status},
        {"node_count", plane.node_count},
        {"stale_nodes", plane.stale_nodes},
        {"degraded_nodes", plane.degraded_nodes},
        {"dropped_frames_total", plane.dropped_frames_total},
        {"latest_sequence", plane.latest_sequence},
        {"max_last_frame_age_ms", plane.max_last_frame_age_ms},
        {"latency",
         nlohmann::json{
             {"max_queue_delay_ms", plane.max_queue_delay_ms},
             {"max_publish_ms", plane.max_publish_ms},
             {"max_controller_ingest_ms", plane.max_controller_ingest_ms},
         }},
        {"gpu_count", plane.gpu_count},
        {"runtime",
         nlohmann::json{
             {"instance_count", plane.plane_instance_count},
             {"ready_instance_count", plane.plane_ready_instance_count},
             {"not_ready_instance_count", plane.plane_not_ready_instance_count},
         }},
        {"avg_gpu_utilization_pct", plane.gpu_utilization_sum / node_count},
    });
  }
  return result;
}

}  // namespace naim::controller
