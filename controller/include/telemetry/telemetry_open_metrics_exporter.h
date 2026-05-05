#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace naim::controller {

class TelemetryOpenMetricsExporter final {
 public:
  std::string Build(
      const nlohmann::json& health,
      const std::optional<std::string>& plane_name) const;

 private:
  std::string SanitizeMetricLabel(std::string value) const;
};

}  // namespace naim::controller
