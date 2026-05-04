#pragma once

#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "naim/runtime/runtime_status.h"

namespace naim::controller {

class TelemetryLiveStore final {
 public:
  struct StreamDelta {
    std::vector<naim::HostTelemetryFrame> frames;
    bool replay_required = false;
    std::string replay_reason;
    std::uint64_t requested_sequence = 0;
    std::uint64_t first_available_sequence = 0;
    std::uint64_t latest_sequence = 0;
    std::uint64_t dropped_frames_total = 0;
  };

  static TelemetryLiveStore& Instance();

  bool Upsert(naim::HostTelemetryFrame frame);
  nlohmann::json BuildSnapshot(
      const std::optional<std::string>& plane_name,
      int history_seconds) const;
  std::vector<naim::HostTelemetryFrame> LoadFramesAfter(
      std::uint64_t sequence,
      const std::optional<std::string>& plane_name) const;
  StreamDelta LoadStreamDeltaAfter(
      std::uint64_t sequence,
      const std::optional<std::string>& plane_name) const;
  std::uint64_t LatestSequence() const;

 private:
  struct NodeBuffer {
    naim::HostTelemetryFrame latest;
    std::deque<naim::HostTelemetryFrame> history;
    std::uint64_t dropped_frames_total = 0;
    std::uint64_t last_pruned_sequence = 0;
  };

  static bool MatchesPlane(
      const naim::HostTelemetryFrame& frame,
      const std::optional<std::string>& plane_name);
  static nlohmann::json FrameToJson(const naim::HostTelemetryFrame& frame);
  static nlohmann::json BuildLatencyBreakdown(
      const naim::HostTelemetryFrame& frame,
      std::uint64_t controller_ingest_delay_ms);
  static nlohmann::json BuildTelemetryHealth(
      const naim::HostTelemetryFrame& frame,
      const NodeBuffer& buffer,
      std::uint64_t now_ms,
      std::uint64_t controller_ingest_delay_ms);
  static nlohmann::json BuildDownsampledHistory(
      const std::vector<const NodeBuffer*>& buffers,
      int history_seconds,
      std::uint64_t now_ms);
  static nlohmann::json BuildPlaneAggregates(
      const std::vector<const NodeBuffer*>& buffers,
      std::uint64_t now_ms);
  static constexpr std::size_t HistoryCapacity();
  static constexpr std::size_t StreamBatchLimit();
  static constexpr std::uint64_t WarmBucketMs();
  static constexpr std::uint64_t ColdBucketMs();

  mutable std::mutex mutex_;
  std::vector<NodeBuffer> nodes_;
  std::uint64_t latest_sequence_ = 0;
  std::uint64_t dropped_frames_total_ = 0;
};

}  // namespace naim::controller
