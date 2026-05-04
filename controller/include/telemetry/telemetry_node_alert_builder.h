#pragma once

#include <cstdint>

#include <nlohmann/json.hpp>

#include "telemetry/telemetry_frame_matcher.h"
#include "telemetry/telemetry_live_store_types.h"
#include "telemetry/telemetry_state_types.h"

namespace naim::controller {

class TelemetryNodeAlertBuilder final {
 public:
  nlohmann::json Build(
      const TelemetryNodeBuffer& buffer,
      const TelemetryAlertThresholds& thresholds,
      std::uint64_t now_ms) const;

 private:
  TelemetryFrameMatcher matcher_;
};

}  // namespace naim::controller
