#pragma once

#include <optional>
#include <string>

#include "http/controller_http_transport.h"
#include "http/controller_http_types.h"
#include "infra/controller_request_support.h"
#include "skills/skills_factory_service.h"

namespace comet::controller {

class SkillsFactoryHttpService final {
 public:
  SkillsFactoryHttpService(
      const ControllerRequestSupport& request_support,
      SkillsFactoryService service);

  std::optional<HttpResponse> HandleRequest(
      const std::string& db_path,
      const std::string& default_artifacts_root,
      const HttpRequest& request) const;

 private:
  const ControllerRequestSupport& request_support_;
  SkillsFactoryService service_;
};

}  // namespace comet::controller
