#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "telemetry/telemetry_state_types.h"

namespace naim::controller {

class TelemetryStreamMetricsService final {
 public:
  void RecordOpened(TelemetryStreamMetrics& streams, const std::string& stream_name) const;
  void RecordClosed(TelemetryStreamMetrics& streams, const std::string& stream_name) const;
  void RecordReplayRequired(
      TelemetryStreamMetrics& streams,
      const std::string& stream_name) const;
  void RecordSendFailure(
      TelemetryStreamMetrics& streams,
      const std::string& stream_name) const;
  nlohmann::json BuildStatus(const TelemetryStreamMetrics& streams) const;

 private:
  TelemetryStreamState& MutableStreamState(
      TelemetryStreamMetrics& streams,
      const std::string& stream_name) const;
  nlohmann::json EncodeState(const TelemetryStreamState& state) const;
};

}  // namespace naim::controller
