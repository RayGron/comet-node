#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "naim/runtime/runtime_status.h"
#include "telemetry/telemetry_live_store_types.h"

namespace naim::controller {

class TelemetryLiveStoreServices;
class TelemetryLiveStoreState;

class TelemetryLiveStore final {
 public:
  using RetentionConfig = TelemetryRetentionConfig;
  using AlertThresholds = TelemetryAlertThresholds;
  using StreamDelta = TelemetryStreamDelta;

  TelemetryLiveStore();
  ~TelemetryLiveStore();

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
  bool UpsertInMemoryLocked(naim::HostTelemetryFrame frame);
  void ConfigurePersistenceLocked(
      const std::string& db_path,
      std::size_t retention_capacity);

  mutable std::mutex mutex_;
  std::unique_ptr<TelemetryLiveStoreState> state_;
  std::unique_ptr<TelemetryLiveStoreServices> services_;
};

}  // namespace naim::controller
