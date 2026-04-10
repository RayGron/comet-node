#pragma once

#include <map>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "http/controller_http_transport.h"
#include "http/controller_http_types.h"
#include "interaction/interaction_http_support.h"
#include "interaction/interaction_types.h"
#include "comet/core/platform_compat.h"

class AuthSupportService;

class InteractionHttpService {
 public:
  explicit InteractionHttpService(InteractionHttpSupport support);

  comet::controller::PlaneInteractionResolution ResolvePlane(
      const std::string& db_path,
      const std::string& plane_name) const;

  comet::controller::InteractionSessionResult ExecuteSession(
      const comet::controller::PlaneInteractionResolution& resolution,
      const comet::controller::InteractionRequestContext& request_context) const;

  std::optional<comet::controller::InteractionValidationError> ResolveRequestSkills(
      const comet::controller::PlaneInteractionResolution& resolution,
      comet::controller::InteractionRequestContext* request_context) const;

  std::optional<comet::controller::InteractionValidationError> ResolveRequestBrowsing(
      const comet::controller::PlaneInteractionResolution& resolution,
      comet::controller::InteractionRequestContext* request_context) const;

  std::optional<comet::controller::InteractionValidationError> ResolveRequestContext(
      const comet::controller::PlaneInteractionResolution& resolution,
      comet::controller::InteractionRequestContext* request_context) const;

  HttpResponse BuildSessionResponse(
      const comet::controller::PlaneInteractionResolution& resolution,
      const comet::controller::InteractionRequestContext& request_context,
      const comet::controller::InteractionSessionResult& result) const;

  HttpResponse ProxyJson(
      const comet::controller::PlaneInteractionResolution& resolution,
      const std::string& request_id,
      const std::string& method,
      const std::string& path,
      const std::string& body = "") const;

 void StreamPlaneInteractionSse(
      comet::platform::SocketHandle client_fd,
      const std::string& db_path,
      const HttpRequest& request,
      AuthSupportService& auth_support) const;

 private:
  InteractionHttpSupport support_;
};
