#include "telemetry/telemetry_live_store.h"

#include <utility>

#include "telemetry/telemetry_live_store_services.h"
#include "telemetry/telemetry_live_store_state.h"
#include "telemetry/telemetry_state_types.h"

namespace naim::controller {

TelemetryLiveStore::TelemetryLiveStore()
    : state_(std::make_unique<TelemetryLiveStoreState>()),
      services_(std::make_unique<TelemetryLiveStoreServices>()) {}

TelemetryLiveStore::~TelemetryLiveStore() = default;

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
      services_->operational_config.RetentionFromEnvironment(state_->retention);
  const auto thresholds =
      services_->operational_config.ThresholdsFromEnvironment(state_->thresholds);
  ConfigureOperationalPolicy(retention, thresholds);
  ConfigurePersistence(db_path, retention.durable_history_capacity);
}

void TelemetryLiveStore::ConfigureOperationalPolicy(
    RetentionConfig retention,
    AlertThresholds thresholds) {
  std::lock_guard<std::mutex> lock(mutex_);
  state_->retention = services_->operational_config.NormalizeRetention(retention);
  state_->thresholds = thresholds;
  services_->ring_buffer.ApplyHotRetention(
      state_->nodes,
      state_->dropped_frames_total,
      state_->retention);
}

void TelemetryLiveStore::ResetForTests() {
  std::lock_guard<std::mutex> lock(mutex_);
  state_->nodes.clear();
  state_->latest_sequence = 0;
  state_->dropped_frames_total = 0;
  state_->retention = RetentionConfig{};
  state_->thresholds = AlertThresholds{};
  state_->persistence = TelemetryPersistenceState{};
  state_->streams = TelemetryStreamMetrics{};
}

bool TelemetryLiveStore::Upsert(naim::HostTelemetryFrame frame) {
  if (frame.node_name.empty()) {
    return false;
  }
  frame = services_->frame_normalizer.Normalize(std::move(frame));
  std::lock_guard<std::mutex> lock(mutex_);
  const bool updated = UpsertInMemoryLocked(frame);
  if (updated) {
    services_->persistence_repository.PersistFrame(state_->persistence, frame);
  }
  return updated;
}

nlohmann::json TelemetryLiveStore::BuildSnapshot(
    const std::optional<std::string>& plane_name,
    const int history_seconds) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return services_->snapshot_builder.BuildSnapshot(
      state_->nodes,
      state_->retention,
      state_->thresholds,
      state_->persistence,
      state_->streams,
      state_->latest_sequence,
      state_->dropped_frames_total,
      plane_name,
      history_seconds,
      services_->clock.CurrentUnixMillis());
}

nlohmann::json TelemetryLiveStore::BuildHealth(
    const std::optional<std::string>& plane_name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return services_->health_builder.BuildHealth(
      state_->nodes,
      state_->retention,
      state_->thresholds,
      state_->persistence,
      state_->streams,
      state_->latest_sequence,
      state_->dropped_frames_total,
      plane_name,
      services_->clock.CurrentUnixMillis());
}

std::string TelemetryLiveStore::BuildOpenMetrics(
    const std::optional<std::string>& plane_name) const {
  return services_->open_metrics_exporter.Build(BuildHealth(plane_name), plane_name);
}

std::vector<naim::HostTelemetryFrame> TelemetryLiveStore::LoadFramesAfter(
    const std::uint64_t sequence,
    const std::optional<std::string>& plane_name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return services_->ring_buffer.LoadFramesAfter(
      state_->nodes,
      state_->retention,
      sequence,
      plane_name);
}

TelemetryLiveStore::StreamDelta TelemetryLiveStore::LoadStreamDeltaAfter(
    const std::uint64_t sequence,
    const std::optional<std::string>& plane_name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return services_->ring_buffer.LoadStreamDeltaAfter(
      state_->nodes,
      state_->retention,
      state_->latest_sequence,
      state_->dropped_frames_total,
      sequence,
      plane_name);
}

std::uint64_t TelemetryLiveStore::LatestSequence() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_->latest_sequence;
}

void TelemetryLiveStore::RecordStreamOpened(const std::string& stream_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  services_->stream_metrics_service.RecordOpened(state_->streams, stream_name);
}

void TelemetryLiveStore::RecordStreamClosed(const std::string& stream_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  services_->stream_metrics_service.RecordClosed(state_->streams, stream_name);
}

void TelemetryLiveStore::RecordStreamReplayRequired(const std::string& stream_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  services_->stream_metrics_service.RecordReplayRequired(state_->streams, stream_name);
}

void TelemetryLiveStore::RecordStreamSendFailure(const std::string& stream_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  services_->stream_metrics_service.RecordSendFailure(state_->streams, stream_name);
}

bool TelemetryLiveStore::UpsertInMemoryLocked(naim::HostTelemetryFrame frame) {
  return services_->ring_buffer.Upsert(
      state_->nodes,
      state_->latest_sequence,
      state_->dropped_frames_total,
      state_->retention,
      std::move(frame));
}

void TelemetryLiveStore::ConfigurePersistenceLocked(
    const std::string& db_path,
    const std::size_t retention_capacity) {
  const auto frames =
      services_->persistence_repository.Configure(
          state_->persistence,
          db_path,
          retention_capacity);
  for (auto frame : frames) {
    frame = services_->frame_normalizer.Normalize(std::move(frame));
    UpsertInMemoryLocked(std::move(frame));
  }
}

}  // namespace naim::controller
