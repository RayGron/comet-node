#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace comet::controller {

class ControllerHealthService {
 public:
  nlohmann::json BuildPayload(const std::string& db_path) const;
};

}  // namespace comet::controller
