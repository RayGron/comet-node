#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "http/controller_http_transport.h"
#include "naim/state/models.h"
#include "interaction/interaction_types.h"

namespace naim::controller {

class PlaneSkillsTargetResolver final {
 public:
  static std::vector<std::pair<std::string, std::string>> DefaultJsonHeaders();
  static const InstanceSpec* FindSkillsInstance(const DesiredState& desired_state);
  static std::optional<ControllerEndpointTarget> ResolvePlaneLocalTarget(
      const DesiredState& desired_state);
  static ::HttpResponse SendPlaneLocalRequest(
      const std::string& db_path,
      const std::string& plane_name,
      const ControllerEndpointTarget& target,
      const std::string& method,
      const std::string& path,
      const std::string& body,
      const std::vector<std::pair<std::string, std::string>>& headers,
      int timeout_ms = 30000);
  static std::string NormalizeSkillPathSuffix(const std::string& path_suffix);
};

}  // namespace naim::controller
