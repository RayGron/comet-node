#include "telemetry/telemetry_alert_builder.h"

namespace naim::controller {

nlohmann::json TelemetryAlertBuilder::Build(
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

}  // namespace naim::controller
