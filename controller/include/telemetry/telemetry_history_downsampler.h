#pragma once

#include <cstdint>
#include <vector>

#include <nlohmann/json.hpp>

#include "telemetry/telemetry_frame_matcher.h"
#include "telemetry/telemetry_state_types.h"

namespace naim::controller {

class TelemetryHistoryDownsampler final {
 public:
  nlohmann::json Build(
      const std::vector<const TelemetryNodeBuffer*>& buffers,
      int history_seconds,
      std::uint64_t now_ms,
      std::uint64_t warm_bucket_ms,
      std::uint64_t cold_bucket_ms) const;

 private:
  TelemetryFrameMatcher matcher_;
};

}  // namespace naim::controller
