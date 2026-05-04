#include "telemetry/telemetry_frame_normalizer.h"

namespace naim::controller {

naim::HostTelemetryFrame TelemetryFrameNormalizer::Normalize(
    naim::HostTelemetryFrame frame) const {
  if (frame.channel.empty()) {
    frame.channel = "host.telemetry.v1";
  }
  if (frame.schema_version.empty()) {
    frame.schema_version = "host.telemetry.v2";
  }
  if (frame.source.empty()) {
    frame.source = "hostd";
  }
  if (frame.node_id.empty()) {
    frame.node_id = frame.node_name;
  }
  if (frame.plane_id.empty()) {
    frame.plane_id = frame.plane_name;
  }
  if (frame.monotonic_timestamp_ms == 0) {
    frame.monotonic_timestamp_ms = frame.monotonic_ms;
  }
  return frame;
}

}  // namespace naim::controller
