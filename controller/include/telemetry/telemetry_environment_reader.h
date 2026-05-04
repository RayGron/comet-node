#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace naim::controller {

class TelemetryEnvironmentReader final {
 public:
  std::optional<std::uint64_t> Uint64(const char* key) const;
  std::optional<std::size_t> Size(const char* key) const;
};

}  // namespace naim::controller
