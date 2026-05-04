#include "telemetry/telemetry_node_alert_builder.h"

namespace naim::controller {

nlohmann::json TelemetryNodeAlertBuilder::Build(
    const TelemetryNodeBuffer& buffer,
    const TelemetryAlertThresholds& thresholds,
    const std::uint64_t now_ms) const {
  nlohmann::json alerts = nlohmann::json::array();
  const auto& frame = buffer.latest;
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
  return alerts;
}

}  // namespace naim::controller
