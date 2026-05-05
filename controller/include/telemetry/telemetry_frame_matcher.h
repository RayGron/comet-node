#pragma once

#include <optional>
#include <string>
#include <vector>

#include "naim/runtime/runtime_status.h"

namespace naim::controller {

class TelemetryFrameMatcher final {
 public:
  bool MatchesPlane(
      const naim::HostTelemetryFrame& frame,
      const std::optional<std::string>& plane_name) const;
  bool RuntimeStatusMatchesPlane(
      const naim::RuntimeProcessStatus& status,
      const std::string& plane_name) const;
  std::string PlaneKeyForFrame(const naim::HostTelemetryFrame& frame) const;
  std::vector<std::string> PlaneKeysForFrame(const naim::HostTelemetryFrame& frame) const;
  double GpuUtilizationAverage(const naim::HostTelemetryFrame& frame) const;
  std::uint64_t ControllerIngestDelayMs(
      const naim::HostTelemetryFrame& frame,
      std::uint64_t now_ms) const;

 private:
  const std::vector<std::string>& RuntimeInstancePrefixes() const;
};

}  // namespace naim::controller
