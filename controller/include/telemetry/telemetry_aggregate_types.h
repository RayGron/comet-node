#pragma once

#include <cstdint>
#include <string>

namespace naim::controller {

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
