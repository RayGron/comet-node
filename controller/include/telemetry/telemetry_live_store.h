#pragma once

#include <cstdint>
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

  void ConfigurePersistence(
      const std::string& db_path,
      std::size_t retention_capacity = 9600);
  void ResetForTests();

  bool Upsert(naim::HostTelemetryFrame frame);
  nlohmann::json BuildSnapshot(
      const std::optional<std::string>& plane_name,
      int history_seconds) const;
  nlohmann::json BuildHealth(
      const std::optional<std::string>& plane_name) const;
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

  struct PersistenceState {
    bool enabled = false;
    std::string db_path;
    std::size_t retention_capacity = 0;
    std::uint64_t loaded_frames_total = 0;
    std::uint64_t persisted_frames_total = 0;
    std::uint64_t pruned_frames_total = 0;
    std::uint64_t error_count = 0;
    std::string last_error;
  };

  static bool MatchesPlane(
      const naim::HostTelemetryFrame& frame,
      const std::optional<std::string>& plane_name);
  bool UpsertInMemoryLocked(naim::HostTelemetryFrame frame);
  void ConfigurePersistenceLocked(
      const std::string& db_path,
      std::size_t retention_capacity);
  void PersistFrameLocked(const naim::HostTelemetryFrame& frame);
  std::vector<naim::HostTelemetryFrame> LoadPersistedFramesLocked(
      const std::string& db_path,
      std::size_t retention_capacity);
  nlohmann::json BuildPersistenceStatusLocked() const;
  static nlohmann::json BuildAlerts(
      const std::vector<const NodeBuffer*>& buffers,
      const PersistenceState& persistence,
      std::uint64_t dropped_frames_total,
      std::uint64_t now_ms);
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
  static constexpr std::size_t DurableHistoryCapacity();

  mutable std::mutex mutex_;
  std::vector<NodeBuffer> nodes_;
  std::uint64_t latest_sequence_ = 0;
  std::uint64_t dropped_frames_total_ = 0;
  PersistenceState persistence_;
};

}  // namespace naim::controller
