#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "naim/runtime/runtime_status.h"

namespace naim::controller {

struct TelemetryRetentionConfig {
  std::size_t hot_history_capacity = 300;
  std::size_t stream_batch_limit = 128;
  std::size_t durable_history_capacity = 9600;
  std::uint64_t warm_bucket_ms = 10000;
  std::uint64_t cold_bucket_ms = 60000;
};

struct TelemetryAlertThresholds {
  std::uint64_t stale_warning_ms = 15000;
  std::uint64_t stale_critical_ms = 60000;
  std::uint64_t ingest_warning_ms = 3000;
  std::uint64_t queue_warning_ms = 1000;
  std::uint64_t browser_apply_warning_ms = 250;
};

struct TelemetryStreamDelta {
  std::vector<naim::HostTelemetryFrame> frames;
  bool replay_required = false;
  std::string replay_reason;
  std::uint64_t requested_sequence = 0;
  std::uint64_t first_available_sequence = 0;
  std::uint64_t latest_sequence = 0;
  std::uint64_t dropped_frames_total = 0;
};

}  // namespace naim::controller
