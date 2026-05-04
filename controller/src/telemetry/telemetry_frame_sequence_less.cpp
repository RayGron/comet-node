#include "telemetry/telemetry_frame_sequence_less.h"

namespace naim::controller {

bool TelemetryFrameSequenceLess::operator()(
    const naim::HostTelemetryFrame& left,
    const naim::HostTelemetryFrame& right) const {
  return left.sequence < right.sequence;
}

}  // namespace naim::controller
