#pragma once

#include <cstdint>

#include <nlohmann/json.hpp>

#include "naim/runtime/runtime_status.h"
#include "telemetry/telemetry_frame_health_policy.h"
#include "telemetry/telemetry_frame_matcher.h"

namespace naim::controller {

class TelemetryFrameJsonBuilder final {
 public:
  nlohmann::json Build(
      const naim::HostTelemetryFrame& frame,
      std::uint64_t now_ms) const;
  nlohmann::json BuildLatencyBreakdown(
      const naim::HostTelemetryFrame& frame,
      std::uint64_t controller_ingest_delay_ms) const;

 private:
  TelemetryFrameHealthPolicy health_policy_;
  TelemetryFrameMatcher matcher_;
};

}  // namespace naim::controller
