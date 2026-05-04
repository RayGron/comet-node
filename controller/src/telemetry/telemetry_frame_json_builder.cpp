#include "telemetry/telemetry_frame_json_builder.h"

#include <string>

namespace naim::controller {

nlohmann::json TelemetryFrameJsonBuilder::Build(
    const naim::HostTelemetryFrame& frame,
    const std::uint64_t now_ms) const {
  auto payload = nlohmann::json::parse(naim::SerializeHostTelemetryFrameJson(frame));
  const std::uint64_t ttl_ms =
      frame.ttl_ms > 0 ? static_cast<std::uint64_t>(frame.ttl_ms) : 0;
  const std::uint64_t expires_at_ms = frame.sequence + ttl_ms;
  const bool stale = frame.sequence == 0 || ttl_ms == 0 || expires_at_ms <= now_ms;
  payload["stale"] = stale;
  payload["expires_in_ms"] = stale ? 0 : expires_at_ms - now_ms;
  const auto controller_ingest_delay_ms = matcher_.ControllerIngestDelayMs(frame, now_ms);
  payload["controller_ingest_delay_ms"] = controller_ingest_delay_ms;
  payload["last_frame_age_ms"] = controller_ingest_delay_ms;
  payload["telemetry_health_status"] =
      stale ? "stale"
            : frame.telemetry_dropped_frames > 0 || !frame.last_publish_error.empty()
                ? "degraded"
                : "ok";
  payload["latency_breakdown"] = BuildLatencyBreakdown(frame, controller_ingest_delay_ms);
  payload["transport"] = nlohmann::json{
      {"primary", "sse"},
      {"fallback", "snapshot-poll"},
      {"sequence", frame.sequence},
      {"schema_version", frame.schema_version},
  };
  return payload;
}

nlohmann::json TelemetryFrameJsonBuilder::BuildLatencyBreakdown(
    const naim::HostTelemetryFrame& frame,
    const std::uint64_t controller_ingest_delay_ms) const {
  const std::uint64_t total_observed_ms =
      frame.collector_duration_ms + frame.publisher_queue_delay_ms +
      frame.publish_duration_ms + controller_ingest_delay_ms;
  return nlohmann::json{
      {"collect_ms", frame.collector_duration_ms},
      {"queue_ms", frame.publisher_queue_delay_ms},
      {"publish_ms", frame.publish_duration_ms},
      {"controller_ingest_ms", controller_ingest_delay_ms},
      {"total_observed_ms", total_observed_ms},
  };
}

}  // namespace naim::controller
