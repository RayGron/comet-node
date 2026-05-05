#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

#include "naim/runtime/runtime_status.h"

namespace naim::controller {

struct TelemetryNodeBuffer {
  naim::HostTelemetryFrame latest;
  std::deque<naim::HostTelemetryFrame> history;
  std::uint64_t dropped_frames_total = 0;
  std::uint64_t last_pruned_sequence = 0;
};

struct TelemetryPersistenceState {
  bool enabled = false;
  std::string db_path;
  std::size_t retention_capacity = 0;
  std::uint64_t loaded_frames_total = 0;
  std::uint64_t persisted_frames_total = 0;
  std::uint64_t pruned_frames_total = 0;
  std::uint64_t error_count = 0;
  std::string last_error;
};

struct TelemetryStreamState {
  std::uint64_t active_clients = 0;
  std::uint64_t opened_total = 0;
  std::uint64_t closed_total = 0;
  std::uint64_t replay_required_total = 0;
  std::uint64_t send_failure_total = 0;
  std::uint64_t reconnect_total = 0;
};

struct TelemetryStreamMetrics {
  TelemetryStreamState telemetry;
  TelemetryStreamState live;
};

}  // namespace naim::controller
