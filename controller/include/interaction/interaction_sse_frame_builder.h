#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace comet::controller {

class InteractionSseFrameBuilder final {
 public:
  std::string BuildEventFrame(
      const std::string& event_name,
      const nlohmann::json& payload) const;

  std::string BuildDoneFrame() const;
};

}  // namespace comet::controller
