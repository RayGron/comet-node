#include "telemetry/telemetry_node_alert_builder.h"

#include <algorithm>

namespace naim::controller {

std::uint64_t TelemetryNodeAlertBuilder::IngestWarningBudgetMs(
    const naim::HostTelemetryFrame& frame,
    const TelemetryAlertThresholds& thresholds) const {
  const std::uint64_t adaptive_interval_ms =
      frame.adaptive_interval_ms > 0
          ? static_cast<std::uint64_t>(frame.adaptive_interval_ms)
          : static_cast<std::uint64_t>(std::max(0, frame.interval_ms));
  return std::max(
      thresholds.ingest_warning_ms,
      adaptive_interval_ms * 2 + thresholds.ingest_warning_ms);
}

bool TelemetryNodeAlertBuilder::HasActivePublishError(
    const naim::HostTelemetryFrame& frame) const {
  return frame.publish_error_count > 0 && !frame.last_publish_error.empty();
}

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
  const auto ingest_warning_budget_ms = IngestWarningBudgetMs(frame, thresholds);
  if (ingest_ms >= ingest_warning_budget_ms) {
    alerts.push_back(nlohmann::json{
        {"code", "telemetry.controller.ingest_delay"},
        {"severity", "warning"},
        {"node_name", frame.node_name},
        {"plane_name", matcher_.PlaneKeyForFrame(frame)},
        {"delay_ms", ingest_ms},
        {"expected_interval_ms", frame.adaptive_interval_ms},
        {"warning_budget_ms", ingest_warning_budget_ms},
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
  if (HasActivePublishError(frame)) {
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
