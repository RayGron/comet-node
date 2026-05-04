#include "telemetry/telemetry_node_health_builder.h"

#include <string>

namespace naim::controller {

nlohmann::json TelemetryNodeHealthBuilder::Build(
    const naim::HostTelemetryFrame& frame,
    const TelemetryNodeBuffer& buffer,
    const std::uint64_t now_ms,
    const std::uint64_t controller_ingest_delay_ms) const {
  const std::uint64_t ttl_ms =
      frame.ttl_ms > 0 ? static_cast<std::uint64_t>(frame.ttl_ms) : 0;
  const bool stale =
      frame.sequence == 0 || ttl_ms == 0 || frame.sequence + ttl_ms <= now_ms;
  std::string status = "ok";
  if (stale) {
    status = "stale";
  } else if (
      buffer.dropped_frames_total > 0 || frame.telemetry_dropped_frames > 0 ||
      frame.publish_error_count > 0 || !frame.degraded_reason.empty()) {
    status = "degraded";
  }
  return nlohmann::json{
      {"status", status},
      {"last_frame_age_ms", controller_ingest_delay_ms},
      {"dropped_frames_total", buffer.dropped_frames_total},
      {"publish_error_count", frame.publish_error_count},
      {"publish_error", frame.last_publish_error},
      {"degraded_reason", frame.degraded_reason},
  };
}

}  // namespace naim::controller
