#include "telemetry/telemetry_live_store.h"

#include <algorithm>

namespace naim::controller {

TelemetryLiveStore& TelemetryLiveStore::Instance() {
  static TelemetryLiveStore store;
  return store;
}

void TelemetryLiveStore::ConfigurePersistence(
    const std::string& db_path,
    const std::size_t retention_capacity) {
  std::lock_guard<std::mutex> lock(mutex_);
  ConfigurePersistenceLocked(db_path, retention_capacity);
}

void TelemetryLiveStore::ConfigureFromEnvironment(const std::string& db_path) {
  const auto retention =
      operational_config_.RetentionFromEnvironment(retention_);
  const auto thresholds =
      operational_config_.ThresholdsFromEnvironment(thresholds_);
  ConfigureOperationalPolicy(retention, thresholds);
  ConfigurePersistence(db_path, retention.durable_history_capacity);
}

void TelemetryLiveStore::ConfigureOperationalPolicy(
    RetentionConfig retention,
    AlertThresholds thresholds) {
  std::lock_guard<std::mutex> lock(mutex_);
  retention_ = operational_config_.NormalizeRetention(retention);
  thresholds_ = thresholds;
  ring_buffer_.ApplyHotRetention(nodes_, dropped_frames_total_, retention_);
}

void TelemetryLiveStore::ResetForTests() {
  std::lock_guard<std::mutex> lock(mutex_);
  nodes_.clear();
  latest_sequence_ = 0;
  dropped_frames_total_ = 0;
  retention_ = RetentionConfig{};
  thresholds_ = AlertThresholds{};
  persistence_ = TelemetryPersistenceState{};
  streams_ = TelemetryStreamMetrics{};
}

bool TelemetryLiveStore::Upsert(naim::HostTelemetryFrame frame) {
  if (frame.node_name.empty()) {
    return false;
  }
  frame = frame_normalizer_.Normalize(std::move(frame));
  std::lock_guard<std::mutex> lock(mutex_);
  const bool updated = UpsertInMemoryLocked(frame);
  if (updated) {
    persistence_repository_.PersistFrame(persistence_, frame);
  }
  return updated;
}

nlohmann::json TelemetryLiveStore::BuildSnapshot(
    const std::optional<std::string>& plane_name,
    const int history_seconds) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_builder_.BuildSnapshot(
      nodes_,
      retention_,
      thresholds_,
      persistence_,
      streams_,
      latest_sequence_,
      dropped_frames_total_,
      plane_name,
      history_seconds,
      clock_.CurrentUnixMillis());
}

nlohmann::json TelemetryLiveStore::BuildHealth(
    const std::optional<std::string>& plane_name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return health_builder_.BuildHealth(
      nodes_,
      retention_,
      thresholds_,
      persistence_,
      streams_,
      latest_sequence_,
      dropped_frames_total_,
      plane_name,
      clock_.CurrentUnixMillis());
}

std::string TelemetryLiveStore::BuildOpenMetrics(
    const std::optional<std::string>& plane_name) const {
  return open_metrics_exporter_.Build(BuildHealth(plane_name), plane_name);
}

std::vector<naim::HostTelemetryFrame> TelemetryLiveStore::LoadFramesAfter(
    const std::uint64_t sequence,
    const std::optional<std::string>& plane_name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ring_buffer_.LoadFramesAfter(nodes_, retention_, sequence, plane_name);
}

TelemetryLiveStore::StreamDelta TelemetryLiveStore::LoadStreamDeltaAfter(
    const std::uint64_t sequence,
    const std::optional<std::string>& plane_name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ring_buffer_.LoadStreamDeltaAfter(
      nodes_,
      retention_,
      latest_sequence_,
      dropped_frames_total_,
      sequence,
      plane_name);
}

std::uint64_t TelemetryLiveStore::LatestSequence() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_sequence_;
}

void TelemetryLiveStore::RecordStreamOpened(const std::string& stream_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  stream_metrics_service_.RecordOpened(streams_, stream_name);
}

void TelemetryLiveStore::RecordStreamClosed(const std::string& stream_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  stream_metrics_service_.RecordClosed(streams_, stream_name);
}

void TelemetryLiveStore::RecordStreamReplayRequired(const std::string& stream_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  stream_metrics_service_.RecordReplayRequired(streams_, stream_name);
}

void TelemetryLiveStore::RecordStreamSendFailure(const std::string& stream_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  stream_metrics_service_.RecordSendFailure(streams_, stream_name);
}

bool TelemetryLiveStore::UpsertInMemoryLocked(naim::HostTelemetryFrame frame) {
  return ring_buffer_.Upsert(
      nodes_,
      latest_sequence_,
      dropped_frames_total_,
      retention_,
      std::move(frame));
}

void TelemetryLiveStore::ConfigurePersistenceLocked(
    const std::string& db_path,
    const std::size_t retention_capacity) {
  const auto frames =
      persistence_repository_.Configure(persistence_, db_path, retention_capacity);
  for (auto frame : frames) {
    frame = frame_normalizer_.Normalize(std::move(frame));
    UpsertInMemoryLocked(std::move(frame));
  }
}

}  // namespace naim::controller
