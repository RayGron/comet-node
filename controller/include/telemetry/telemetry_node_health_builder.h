#pragma once

#include <cstdint>

#include <nlohmann/json.hpp>

#include "naim/runtime/runtime_status.h"
#include "telemetry/telemetry_state_types.h"

namespace naim::controller {

class TelemetryNodeHealthBuilder final {
 public:
  nlohmann::json Build(
      const naim::HostTelemetryFrame& frame,
      const TelemetryNodeBuffer& buffer,
      std::uint64_t now_ms,
      std::uint64_t controller_ingest_delay_ms) const;
};

}  // namespace naim::controller
