#include "telemetry/telemetry_environment_reader.h"

#include <cstdlib>
#include <string>

namespace naim::controller {

std::optional<std::uint64_t> TelemetryEnvironmentReader::Uint64(const char* key) const {
  const char* value = std::getenv(key);
  if (value == nullptr || value[0] == '\0') {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(std::stoull(value));
}

std::optional<std::size_t> TelemetryEnvironmentReader::Size(const char* key) const {
  const auto value = Uint64(key);
  if (!value.has_value()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(*value);
}

}  // namespace naim::controller
