#pragma once

#include <cstdint>
#include <vector>

#include <nlohmann/json.hpp>

#include "telemetry/telemetry_frame_matcher.h"
#include "telemetry/telemetry_node_health_builder.h"
#include "telemetry/telemetry_state_types.h"

namespace naim::controller {

class TelemetryPlaneAggregateBuilder final {
 public:
  nlohmann::json Build(
      const std::vector<const TelemetryNodeBuffer*>& buffers,
      std::uint64_t now_ms) const;

 private:
  TelemetryFrameMatcher matcher_;
  TelemetryNodeHealthBuilder node_health_builder_;
};

}  // namespace naim::controller
