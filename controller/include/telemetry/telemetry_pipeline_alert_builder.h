#pragma once

#include <cstdint>

#include <nlohmann/json.hpp>

#include "telemetry/telemetry_state_types.h"

namespace naim::controller {

class TelemetryPipelineAlertBuilder final {
 public:
  nlohmann::json Build(
      const TelemetryPersistenceState& persistence,
      const TelemetryStreamMetrics& streams,
      std::uint64_t dropped_frames_total) const;
};

}  // namespace naim::controller
