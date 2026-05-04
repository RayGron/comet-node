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
  struct RetentionConfig {
    std::size_t hot_history_capacity = 300;
    std::size_t stream_batch_limit = 128;
    std::size_t durable_history_capacity = 9600;
    std::uint64_t warm_bucket_ms = 10000;
    std::uint64_t cold_bucket_ms = 60000;
  };

  struct AlertThresholds {
    std::uint64_t stale_warning_ms = 15000;
    std::uint64_t stale_critical_ms = 60000;
    std::uint64_t ingest_warning_ms = 3000;
    std::uint64_t queue_warning_ms = 1000;
    std::uint64_t browser_apply_warning_ms = 250;
  };

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
  void ConfigureFromEnvironment(const std::string& db_path);
  void ConfigureOperationalPolicy(
      RetentionConfig retention,
      AlertThresholds thresholds);
  void ResetForTests();

  bool Upsert(naim::HostTelemetryFrame frame);
  nlohmann::json BuildSnapshot(
      const std::optional<std::string>& plane_name,
      int history_seconds) const;
  nlohmann::json BuildHealth(
      const std::optional<std::string>& plane_name) const;
  std::string BuildOpenMetrics(
      const std::optional<std::string>& plane_name) const;
  std::vector<naim::HostTelemetryFrame> LoadFramesAfter(
      std::uint64_t sequence,
      const std::optional<std::string>& plane_name) const;
  StreamDelta LoadStreamDeltaAfter(
      std::uint64_t sequence,
      const std::optional<std::string>& plane_name) const;
  std::uint64_t LatestSequence() const;
  void RecordStreamOpened(const std::string& stream_name);
  void RecordStreamClosed(const std::string& stream_name);
  void RecordStreamReplayRequired(const std::string& stream_name);
  void RecordStreamSendFailure(const std::string& stream_name);

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

  struct StreamState {
    std::uint64_t active_clients = 0;
    std::uint64_t opened_total = 0;
    std::uint64_t closed_total = 0;
    std::uint64_t replay_required_total = 0;
    std::uint64_t send_failure_total = 0;
    std::uint64_t reconnect_total = 0;
  };

  struct StreamMetrics {
    StreamState telemetry;
    StreamState live;
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
  nlohmann::json BuildStreamStatusLocked() const;
  static nlohmann::json BuildAlerts(
      const std::vector<const NodeBuffer*>& buffers,
      const PersistenceState& persistence,
      const StreamMetrics& streams,
      const AlertThresholds& thresholds,
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
      std::uint64_t now_ms,
      std::uint64_t warm_bucket_ms,
      std::uint64_t cold_bucket_ms);
  static nlohmann::json BuildPlaneAggregates(
      const std::vector<const NodeBuffer*>& buffers,
      std::uint64_t now_ms);
  void RecordStreamOpenedLocked(const std::string& stream_name);
  StreamState& MutableStreamStateLocked(const std::string& stream_name);

  mutable std::mutex mutex_;
  std::vector<NodeBuffer> nodes_;
  std::uint64_t latest_sequence_ = 0;
  std::uint64_t dropped_frames_total_ = 0;
  RetentionConfig retention_;
  AlertThresholds thresholds_;
  PersistenceState persistence_;
  StreamMetrics streams_;
};

}  // namespace naim::controller
