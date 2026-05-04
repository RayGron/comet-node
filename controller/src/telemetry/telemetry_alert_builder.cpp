#include "telemetry/telemetry_alert_builder.h"

namespace naim::controller {

nlohmann::json TelemetryAlertBuilder::Build(
    const std::vector<const TelemetryNodeBuffer*>& buffers,
    const TelemetryPersistenceState& persistence,
    const TelemetryStreamMetrics& streams,
    const TelemetryAlertThresholds& thresholds,
    const std::uint64_t now_ms) const {
  nlohmann::json alerts = nlohmann::json::array();
  const auto pipeline_alerts = pipeline_alert_builder_.Build(persistence, streams);
  alerts.insert(alerts.end(), pipeline_alerts.begin(), pipeline_alerts.end());
  for (const auto* buffer : buffers) {
    if (buffer == nullptr) {
      continue;
    }
    const auto node_alerts = node_alert_builder_.Build(*buffer, thresholds, now_ms);
    alerts.insert(alerts.end(), node_alerts.begin(), node_alerts.end());
  }
  return alerts;
}

}  // namespace naim::controller
