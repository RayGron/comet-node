#pragma once

#include <nlohmann/json.hpp>

#include "telemetry/telemetry_state_types.h"

namespace naim::controller {

class TelemetryPersistenceStatusBuilder final {
 public:
  nlohmann::json Build(const TelemetryPersistenceState& state) const;
};

}  // namespace naim::controller
