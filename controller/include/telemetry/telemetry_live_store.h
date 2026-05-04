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
  static TelemetryLiveStore& Instance();

  bool Upsert(naim::HostTelemetryFrame frame);
  nlohmann::json BuildSnapshot(
      const std::optional<std::string>& plane_name,
      int history_seconds) const;
  std::vector<naim::HostTelemetryFrame> LoadFramesAfter(
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
  static constexpr std::size_t HistoryCapacity();
  static constexpr std::size_t StreamBatchLimit();

  mutable std::mutex mutex_;
  std::vector<NodeBuffer> nodes_;
  std::uint64_t latest_sequence_ = 0;
  std::uint64_t dropped_frames_total_ = 0;
};

}  // namespace naim::controller
