#pragma once

#include <cstdint>
#include <vector>

#include "telemetry/telemetry_live_store_types.h"
#include "telemetry/telemetry_state_types.h"

namespace naim::controller {

class TelemetryLiveStoreState final {
 public:
  TelemetryLiveStoreState();

  std::vector<TelemetryNodeBuffer> nodes;
  std::uint64_t latest_sequence = 0;
  std::uint64_t dropped_frames_total = 0;
  TelemetryRetentionConfig retention;
  TelemetryAlertThresholds thresholds;
  TelemetryPersistenceState persistence;
  TelemetryStreamMetrics streams;
};

}  // namespace naim::controller
