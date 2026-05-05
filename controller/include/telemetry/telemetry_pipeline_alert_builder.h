#pragma once

#include <nlohmann/json.hpp>

#include "telemetry/telemetry_state_types.h"

namespace naim::controller {

class TelemetryPipelineAlertBuilder final {
 public:
  nlohmann::json Build(
      const TelemetryPersistenceState& persistence,
      const TelemetryStreamMetrics& streams) const;
};

}  // namespace naim::controller
