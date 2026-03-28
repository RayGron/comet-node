#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "http/controller_http_types.h"

namespace comet::controller {

class ControllerRequestSupport {
 public:
  nlohmann::json ParseJsonRequestBody(const HttpRequest& request) const;

  std::string ResolveArtifactsRoot(
      const std::optional<std::string>& artifacts_root_arg,
      const std::string& fallback_artifacts_root) const;
};

}  // namespace comet::controller
