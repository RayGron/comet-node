#include "telemetry/telemetry_history_downsampler.h"

#include <algorithm>
#include <map>
#include <string>

#include "telemetry/telemetry_aggregate_types.h"

namespace naim::controller {

nlohmann::json TelemetryHistoryDownsampler::Build(
    const std::vector<const TelemetryNodeBuffer*>& buffers,
    const int history_seconds,
    const std::uint64_t now_ms,
    const std::uint64_t warm_bucket_ms,
    const std::uint64_t cold_bucket_ms) const {
  if (history_seconds <= 0) {
    return nlohmann::json::array();
  }
  std::map<std::string, TelemetryHistoryBucketAccumulator> buckets;
  const std::uint64_t horizon_ms =
      static_cast<std::uint64_t>(std::max(1, history_seconds)) * 1000ULL;
  for (const auto* buffer : buffers) {
    if (buffer == nullptr) {
      continue;
    }
    for (const auto& frame : buffer->history) {
      if (frame.sequence == 0 || now_ms > frame.sequence + horizon_ms) {
        continue;
      }
      const std::uint64_t age_ms = now_ms >= frame.sequence ? now_ms - frame.sequence : 0;
      const std::uint64_t bucket_ms =
          age_ms <= warm_bucket_ms ? warm_bucket_ms : cold_bucket_ms;
      const std::uint64_t bucket_start_ms = (frame.sequence / bucket_ms) * bucket_ms;
      const auto key =
          frame.node_name + "|" + matcher_.PlaneKeyForFrame(frame) + "|" +
          std::to_string(bucket_ms) + "|" + std::to_string(bucket_start_ms);
      auto& bucket = buckets[key];
      if (bucket.sample_count == 0) {
        bucket.node_name = frame.node_name;
        bucket.plane_name = matcher_.PlaneKeyForFrame(frame);
        bucket.bucket_start_ms = bucket_start_ms;
        bucket.bucket_ms = bucket_ms;
        bucket.first_sequence = frame.sequence;
      }
      bucket.last_sequence = std::max(bucket.last_sequence, frame.sequence);
      bucket.sample_count += 1;
      bucket.cpu_utilization_sum += frame.cpu.utilization_pct;
      const double gpu_util = matcher_.GpuUtilizationAverage(frame);
      bucket.gpu_utilization_sum += gpu_util;
      bucket.max_gpu_utilization_pct = std::max(bucket.max_gpu_utilization_pct, gpu_util);
      bucket.max_queue_delay_ms =
          std::max(bucket.max_queue_delay_ms, frame.publisher_queue_delay_ms);
      bucket.max_publish_ms = std::max(bucket.max_publish_ms, frame.publish_duration_ms);
      bucket.max_controller_ingest_ms = std::max(
          bucket.max_controller_ingest_ms,
          matcher_.ControllerIngestDelayMs(frame, now_ms));
    }
  }
  nlohmann::json result = nlohmann::json::array();
  for (const auto& [_, bucket] : buckets) {
    const double count = static_cast<double>(std::max<std::uint64_t>(1, bucket.sample_count));
    result.push_back(nlohmann::json{
        {"node_name", bucket.node_name},
        {"plane_name", bucket.plane_name},
        {"bucket_start_ms", bucket.bucket_start_ms},
        {"bucket_ms", bucket.bucket_ms},
        {"sample_count", bucket.sample_count},
        {"first_sequence", bucket.first_sequence},
        {"last_sequence", bucket.last_sequence},
        {"avg_cpu_utilization_pct", bucket.cpu_utilization_sum / count},
        {"avg_gpu_utilization_pct", bucket.gpu_utilization_sum / count},
        {"max_gpu_utilization_pct", bucket.max_gpu_utilization_pct},
        {"max_queue_delay_ms", bucket.max_queue_delay_ms},
        {"max_publish_ms", bucket.max_publish_ms},
        {"max_controller_ingest_ms", bucket.max_controller_ingest_ms},
    });
  }
  return result;
}

}  // namespace naim::controller
