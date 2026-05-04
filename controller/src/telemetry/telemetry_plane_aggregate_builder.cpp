#include "telemetry/telemetry_plane_aggregate_builder.h"

#include <algorithm>
#include <map>
#include <string>

#include "telemetry/telemetry_aggregate_types.h"

namespace naim::controller {

nlohmann::json TelemetryPlaneAggregateBuilder::Build(
    const std::vector<const TelemetryNodeBuffer*>& buffers,
    const std::uint64_t now_ms) const {
  std::map<std::string, TelemetryPlaneAccumulator> planes;
  for (const auto* buffer : buffers) {
    if (buffer == nullptr) {
      continue;
    }
    const auto& frame = buffer->latest;
    auto& plane = planes[matcher_.PlaneKeyForFrame(frame)];
    if (plane.plane_name.empty()) {
      plane.plane_name = matcher_.PlaneKeyForFrame(frame);
    }
    const auto ingest_ms = matcher_.ControllerIngestDelayMs(frame, now_ms);
    const auto health = node_health_builder_.Build(frame, *buffer, now_ms, ingest_ms);
    plane.node_count += 1;
    plane.latest_sequence = std::max(plane.latest_sequence, frame.sequence);
    plane.dropped_frames_total += buffer->dropped_frames_total;
    plane.max_last_frame_age_ms = std::max(plane.max_last_frame_age_ms, ingest_ms);
    plane.max_queue_delay_ms = std::max(plane.max_queue_delay_ms, frame.publisher_queue_delay_ms);
    plane.max_publish_ms = std::max(plane.max_publish_ms, frame.publish_duration_ms);
    plane.max_controller_ingest_ms = std::max(plane.max_controller_ingest_ms, ingest_ms);
    plane.gpu_count += frame.gpu.devices.size();
    plane.plane_instance_count += frame.plane_instance_count;
    plane.plane_ready_instance_count += frame.plane_ready_instance_count;
    plane.plane_not_ready_instance_count += frame.plane_not_ready_instance_count;
    plane.gpu_utilization_sum += matcher_.GpuUtilizationAverage(frame);
    if (health.value("status", std::string{"ok"}) == "stale") {
      plane.stale_nodes += 1;
    } else if (health.value("status", std::string{"ok"}) == "degraded") {
      plane.degraded_nodes += 1;
    }
  }
  nlohmann::json result = nlohmann::json::array();
  for (const auto& [_, plane] : planes) {
    std::string status = "ok";
    if (plane.stale_nodes > 0) {
      status = "stale";
    } else if (plane.degraded_nodes > 0) {
      status = "degraded";
    }
    const double node_count = static_cast<double>(std::max<std::uint64_t>(1, plane.node_count));
    result.push_back(nlohmann::json{
        {"plane_name", plane.plane_name},
        {"status", status},
        {"node_count", plane.node_count},
        {"stale_nodes", plane.stale_nodes},
        {"degraded_nodes", plane.degraded_nodes},
        {"dropped_frames_total", plane.dropped_frames_total},
        {"latest_sequence", plane.latest_sequence},
        {"max_last_frame_age_ms", plane.max_last_frame_age_ms},
        {"latency",
         nlohmann::json{
             {"max_queue_delay_ms", plane.max_queue_delay_ms},
             {"max_publish_ms", plane.max_publish_ms},
             {"max_controller_ingest_ms", plane.max_controller_ingest_ms},
         }},
        {"gpu_count", plane.gpu_count},
        {"runtime",
         nlohmann::json{
             {"instance_count", plane.plane_instance_count},
             {"ready_instance_count", plane.plane_ready_instance_count},
             {"not_ready_instance_count", plane.plane_not_ready_instance_count},
         }},
        {"avg_gpu_utilization_pct", plane.gpu_utilization_sum / node_count},
    });
  }
  return result;
}

}  // namespace naim::controller
