#pragma once

#include "naim/runtime/runtime_status.h"

namespace naim::controller {

class TelemetryFrameHealthPolicy final {
 public:
  bool HasActionableDegradedReason(const naim::HostTelemetryFrame& frame) const;

 private:
  bool HasGpuBoundRuntime(const naim::HostTelemetryFrame& frame) const;
};

}  // namespace naim::controller
