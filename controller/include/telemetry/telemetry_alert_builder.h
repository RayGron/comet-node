#pragma once

#include <cstdint>
#include <vector>

#include <nlohmann/json.hpp>

#include "telemetry/telemetry_live_store_types.h"
#include "telemetry/telemetry_node_alert_builder.h"
#include "telemetry/telemetry_pipeline_alert_builder.h"
#include "telemetry/telemetry_state_types.h"

namespace naim::controller {

class TelemetryAlertBuilder final {
 public:
  nlohmann::json Build(
      const std::vector<const TelemetryNodeBuffer*>& buffers,
      const TelemetryPersistenceState& persistence,
      const TelemetryStreamMetrics& streams,
      const TelemetryAlertThresholds& thresholds,
      std::uint64_t dropped_frames_total,
      std::uint64_t now_ms) const;

 private:
  TelemetryPipelineAlertBuilder pipeline_alert_builder_;
  TelemetryNodeAlertBuilder node_alert_builder_;
};

}  // namespace naim::controller
