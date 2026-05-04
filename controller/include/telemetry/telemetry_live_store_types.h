#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
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

struct TelemetryHistoryBucketAccumulator {
  std::string node_name;
  std::string plane_name;
  std::uint64_t bucket_start_ms = 0;
  std::uint64_t bucket_ms = 0;
  std::uint64_t first_sequence = 0;
  std::uint64_t last_sequence = 0;
  std::uint64_t sample_count = 0;
  double cpu_utilization_sum = 0.0;
  double gpu_utilization_sum = 0.0;
  double max_gpu_utilization_pct = 0.0;
  std::uint64_t max_queue_delay_ms = 0;
  std::uint64_t max_publish_ms = 0;
  std::uint64_t max_controller_ingest_ms = 0;
};

struct TelemetryPlaneAccumulator {
  std::string plane_name;
  std::uint64_t node_count = 0;
  std::uint64_t stale_nodes = 0;
  std::uint64_t degraded_nodes = 0;
  std::uint64_t dropped_frames_total = 0;
  std::uint64_t latest_sequence = 0;
  std::uint64_t max_last_frame_age_ms = 0;
  std::uint64_t max_queue_delay_ms = 0;
  std::uint64_t max_publish_ms = 0;
  std::uint64_t max_controller_ingest_ms = 0;
  std::uint64_t gpu_count = 0;
  std::uint64_t plane_instance_count = 0;
  std::uint64_t plane_ready_instance_count = 0;
  std::uint64_t plane_not_ready_instance_count = 0;
  double gpu_utilization_sum = 0.0;
};

}  // namespace naim::controller
