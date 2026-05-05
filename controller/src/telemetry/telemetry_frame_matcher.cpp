#include "telemetry/telemetry_frame_matcher.h"

#include <algorithm>
#include <set>

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
  for (const auto& status : frame.instance_runtime) {
    if (RuntimeStatusMatchesPlane(status, *plane_name)) {
      return true;
    }
  }
  return false;
}

bool TelemetryFrameMatcher::RuntimeStatusMatchesPlane(
    const naim::RuntimeProcessStatus& status,
    const std::string& plane_name) const {
  if (plane_name.empty()) {
    return true;
  }
  if (status.plane_name == plane_name) {
    return true;
  }
  for (const auto& prefix : RuntimeInstancePrefixes()) {
    const std::string stem = prefix + plane_name;
    if (status.instance_name == stem || status.instance_name.rfind(stem + "-", 0) == 0) {
      return true;
    }
  }
  return false;
}

std::string TelemetryFrameMatcher::PlaneKeyForFrame(
    const naim::HostTelemetryFrame& frame) const {
  return frame.plane_name.empty() ? std::string{"unassigned"} : frame.plane_name;
}

std::vector<std::string> TelemetryFrameMatcher::PlaneKeysForFrame(
    const naim::HostTelemetryFrame& frame) const {
  if (!frame.plane_name.empty()) {
    return {frame.plane_name};
  }
  std::set<std::string> plane_names;
  for (const auto& status : frame.instance_runtime) {
    if (!status.plane_name.empty()) {
      plane_names.insert(status.plane_name);
    }
  }
  if (plane_names.empty()) {
    return {"unassigned"};
  }
  return {plane_names.begin(), plane_names.end()};
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

const std::vector<std::string>& TelemetryFrameMatcher::RuntimeInstancePrefixes() const {
  static const std::vector<std::string> prefixes = {
      "infer-",
      "worker-",
      "skills-",
      "app-",
      "webgateway-",
      "browsing-",
      "voice-module-",
      "interaction-",
  };
  return prefixes;
}

}  // namespace naim::controller
