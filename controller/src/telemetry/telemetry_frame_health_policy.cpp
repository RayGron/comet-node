#include "telemetry/telemetry_frame_health_policy.h"

namespace naim::controller {

bool TelemetryFrameHealthPolicy::HasGpuBoundRuntime(
    const naim::HostTelemetryFrame& frame) const {
  for (const auto& runtime : frame.instance_runtime) {
    if (!runtime.gpu_device.empty()) {
      return true;
    }
  }
  return false;
}

bool TelemetryFrameHealthPolicy::HasActionableDegradedReason(
    const naim::HostTelemetryFrame& frame) const {
  if (frame.degraded_reason.empty()) {
    return false;
  }
  if (frame.degraded_reason != "gpu:unavailable") {
    return true;
  }
  return !frame.gpu.devices.empty() || HasGpuBoundRuntime(frame);
}

}  // namespace naim::controller
