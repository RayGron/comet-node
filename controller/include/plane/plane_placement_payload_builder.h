#pragma once

#include <optional>
#include <set>

#include <nlohmann/json.hpp>

#include "comet/state/models.h"

namespace comet::controller {

class PlanePlacementPayloadBuilder final {
 public:
  explicit PlanePlacementPayloadBuilder(const comet::DesiredState& desired_state);

  nlohmann::json Build() const;

 private:
  std::optional<std::string> ResolveExternalAppHostAuthMode() const;
  std::optional<std::string> FindFirstInstanceNodeName(comet::InstanceRole role) const;
  std::set<std::string> FindInstanceNodeNames(comet::InstanceRole role) const;

  const comet::DesiredState& desired_state_;
};

}  // namespace comet::controller
