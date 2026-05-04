#include "telemetry/telemetry_clock.h"

#include <chrono>

namespace naim::controller {

std::uint64_t TelemetryClock::CurrentUnixMillis() const {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

}  // namespace naim::controller
