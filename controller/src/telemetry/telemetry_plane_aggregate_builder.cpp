#include "telemetry/telemetry_plane_aggregate_builder.h"

#include <algorithm>
#include <map>
#include <string>

#include "telemetry/telemetry_aggregate_types.h"

namespace naim::controller {

namespace {

void AddFrameToPlane(
    TelemetryPlaneAccumulator& plane,
    const std::string& plane_name,
    const naim::HostTelemetryFrame& frame,
    const TelemetryNodeBuffer& buffer,
    const nlohmann::json& health,
    const std::uint64_t ingest_ms,
    const std::uint64_t scoped_instance_count,
    const std::uint64_t scoped_ready_instance_count,
    const std::uint64_t scoped_gpu_count,
    const double gpu_utilization_average) {
  if (plane.plane_name.empty()) {
    plane.plane_name = plane_name;
  }
  plane.node_count += 1;
  plane.latest_sequence = std::max(plane.latest_sequence, frame.sequence);
  plane.dropped_frames_total += buffer.dropped_frames_total;
  plane.max_last_frame_age_ms = std::max(plane.max_last_frame_age_ms, ingest_ms);
  plane.max_queue_delay_ms = std::max(plane.max_queue_delay_ms, frame.publisher_queue_delay_ms);
  plane.max_publish_ms = std::max(plane.max_publish_ms, frame.publish_duration_ms);
  plane.max_controller_ingest_ms = std::max(plane.max_controller_ingest_ms, ingest_ms);
  plane.gpu_count += scoped_gpu_count;
  plane.plane_instance_count += scoped_instance_count;
  plane.plane_ready_instance_count += scoped_ready_instance_count;
  plane.plane_not_ready_instance_count += scoped_instance_count - scoped_ready_instance_count;
  plane.gpu_utilization_sum += gpu_utilization_average;
  if (health.value("status", std::string{"ok"}) == "stale") {
    plane.stale_nodes += 1;
  } else if (health.value("status", std::string{"ok"}) == "degraded") {
    plane.degraded_nodes += 1;
  }
}

}  // namespace

nlohmann::json TelemetryPlaneAggregateBuilder::Build(
    const std::vector<const TelemetryNodeBuffer*>& buffers,
    const std::uint64_t now_ms) const {
  std::map<std::string, TelemetryPlaneAccumulator> planes;
  for (const auto* buffer : buffers) {
    if (buffer == nullptr) {
      continue;
    }
    const auto& frame = buffer->latest;
    const auto ingest_ms = matcher_.ControllerIngestDelayMs(frame, now_ms);
    const auto health = node_health_builder_.Build(frame, *buffer, now_ms, ingest_ms);
    const auto plane_keys = matcher_.PlaneKeysForFrame(frame);
    for (const auto& plane_key : plane_keys) {
      std::uint64_t scoped_instance_count = frame.plane_instance_count;
      std::uint64_t scoped_ready_instance_count = frame.plane_ready_instance_count;
      if (frame.plane_name.empty() && plane_key != "unassigned") {
        scoped_instance_count = 0;
        scoped_ready_instance_count = 0;
        for (const auto& status : frame.instance_runtime) {
          if (!matcher_.RuntimeStatusMatchesPlane(status, plane_key)) {
            continue;
          }
          ++scoped_instance_count;
          if (status.ready) {
            ++scoped_ready_instance_count;
          }
        }
      }
      AddFrameToPlane(
          planes[plane_key],
          plane_key,
          frame,
          *buffer,
          health,
          ingest_ms,
          scoped_instance_count,
          scoped_ready_instance_count,
          frame.gpu.devices.size(),
          matcher_.GpuUtilizationAverage(frame));
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
