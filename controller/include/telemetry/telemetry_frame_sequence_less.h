#pragma once

#include "naim/runtime/runtime_status.h"

namespace naim::controller {

class TelemetryFrameSequenceLess final {
 public:
  bool operator()(
      const naim::HostTelemetryFrame& left,
      const naim::HostTelemetryFrame& right) const;
};

}  // namespace naim::controller
