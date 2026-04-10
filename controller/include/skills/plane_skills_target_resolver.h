#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "comet/state/models.h"
#include "interaction/interaction_types.h"

namespace comet::controller {

class PlaneSkillsTargetResolver final {
 public:
  static std::vector<std::pair<std::string, std::string>> DefaultJsonHeaders();
  static const InstanceSpec* FindSkillsInstance(const DesiredState& desired_state);
  static std::optional<ControllerEndpointTarget> ResolvePlaneLocalTarget(
      const DesiredState& desired_state);
  static std::string NormalizeSkillPathSuffix(const std::string& path_suffix);
};

}  // namespace comet::controller
