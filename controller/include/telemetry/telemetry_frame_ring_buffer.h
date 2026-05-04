#pragma once

#include <optional>
#include <string>
#include <vector>

#include "telemetry/telemetry_frame_matcher.h"
#include "telemetry/telemetry_live_store_types.h"

namespace naim::controller {

class TelemetryFrameRingBuffer final {
 public:
  bool Upsert(
      std::vector<TelemetryNodeBuffer>& nodes,
      std::uint64_t& latest_sequence,
      std::uint64_t& dropped_frames_total,
      const TelemetryRetentionConfig& retention,
      naim::HostTelemetryFrame frame) const;
  void ApplyHotRetention(
      std::vector<TelemetryNodeBuffer>& nodes,
      std::uint64_t& dropped_frames_total,
      const TelemetryRetentionConfig& retention) const;
  std::vector<naim::HostTelemetryFrame> LoadFramesAfter(
      const std::vector<TelemetryNodeBuffer>& nodes,
      const TelemetryRetentionConfig& retention,
      std::uint64_t sequence,
      const std::optional<std::string>& plane_name) const;
  TelemetryStreamDelta LoadStreamDeltaAfter(
      const std::vector<TelemetryNodeBuffer>& nodes,
      const TelemetryRetentionConfig& retention,
      std::uint64_t latest_sequence,
      std::uint64_t dropped_frames_total,
      std::uint64_t sequence,
      const std::optional<std::string>& plane_name) const;

 private:
  TelemetryFrameMatcher matcher_;
};

}  // namespace naim::controller
