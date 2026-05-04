#include "telemetry/telemetry_pipeline_alert_builder.h"

namespace naim::controller {

nlohmann::json TelemetryPipelineAlertBuilder::Build(
    const TelemetryPersistenceState& persistence,
    const TelemetryStreamMetrics& streams,
    const std::uint64_t dropped_frames_total) const {
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
  return alerts;
}

}  // namespace naim::controller
