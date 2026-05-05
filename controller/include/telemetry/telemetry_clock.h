#pragma once

#include <cstdint>

namespace naim::controller {

class TelemetryClock final {
 public:
  std::uint64_t CurrentUnixMillis() const;
};

}  // namespace naim::controller
