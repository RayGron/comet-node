#pragma once

#include "telemetry/telemetry_live_store_types.h"

namespace naim::controller {

class TelemetryOperationalConfig final {
 public:
  TelemetryRetentionConfig RetentionFromEnvironment(
      TelemetryRetentionConfig retention) const;
  TelemetryAlertThresholds ThresholdsFromEnvironment(
      TelemetryAlertThresholds thresholds) const;
  TelemetryRetentionConfig NormalizeRetention(
      TelemetryRetentionConfig retention) const;
};

}  // namespace naim::controller
