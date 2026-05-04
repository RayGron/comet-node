#include "telemetry/telemetry_frame_matcher.h"

#include <algorithm>

namespace naim::controller {

bool TelemetryFrameMatcher::MatchesPlane(
    const naim::HostTelemetryFrame& frame,
    const std::optional<std::string>& plane_name) const {
  if (!plane_name.has_value() || plane_name->empty()) {
    return true;
  }
  if (frame.plane_name == *plane_name) {
    return true;
  }
  return std::any_of(
      frame.instance_runtime.begin(),
      frame.instance_runtime.end(),
      [&](const naim::RuntimeProcessStatus& status) {
        return status.instance_name.rfind(*plane_name + "-", 0) == 0;
      });
}

std::string TelemetryFrameMatcher::PlaneKeyForFrame(
    const naim::HostTelemetryFrame& frame) const {
  return frame.plane_name.empty() ? std::string{"unassigned"} : frame.plane_name;
}

double TelemetryFrameMatcher::GpuUtilizationAverage(
    const naim::HostTelemetryFrame& frame) const {
  if (frame.gpu.devices.empty()) {
    return 0.0;
  }
  double sum = 0.0;
  for (const auto& device : frame.gpu.devices) {
    sum += static_cast<double>(device.gpu_utilization_pct);
  }
  return sum / static_cast<double>(frame.gpu.devices.size());
}

std::uint64_t TelemetryFrameMatcher::ControllerIngestDelayMs(
    const naim::HostTelemetryFrame& frame,
    const std::uint64_t now_ms) const {
  return frame.sequence > 0 && now_ms >= frame.sequence ? now_ms - frame.sequence : 0;
}

}  // namespace naim::controller
