#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "http/controller_http_transport.h"
#include "naim/state/models.h"

namespace naim::controller {

class PlaneVoiceService final {
 public:
  bool IsEnabled(const DesiredState& desired_state) const;

  std::optional<ControllerEndpointTarget> ResolveTarget(
      const DesiredState& desired_state) const;

  nlohmann::json BuildStatusPayload(
      const std::string& db_path,
      const DesiredState& desired_state,
      const std::optional<std::string>& plane_state) const;

  std::optional<HttpResponse> ProxyPlaneVoiceRequest(
      const std::string& db_path,
      const DesiredState& desired_state,
      const std::string& method,
      const std::string& path_suffix,
      const std::string& body,
      const std::vector<std::pair<std::string, std::string>>& headers,
      std::string* error_code,
      std::string* error_message) const;
};

}  // namespace naim::controller
