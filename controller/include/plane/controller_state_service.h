#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace comet::controller {

class ControllerStateService {
 public:
  nlohmann::json BuildPayload(
      const std::string& db_path,
      const std::optional<std::string>& plane_name) const;
};

}  // namespace comet::controller
