#pragma once

#include "naim/runtime/runtime_status.h"

namespace naim::controller {

class TelemetryFrameNormalizer final {
 public:
  naim::HostTelemetryFrame Normalize(naim::HostTelemetryFrame frame) const;
};

}  // namespace naim::controller
