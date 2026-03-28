#pragma once

#include <optional>
#include <string>

#include "scheduler/assignment_orchestration_service.h"
#include "auth/auth_support_service.h"
#include "bundle/bundle_cli_service.h"

namespace comet::controller {

class ControllerServeService {
 public:
  int Serve(
      const std::string& db_path,
      const std::string& artifacts_root,
      const std::string& listen_host,
      int listen_port,
      const std::optional<std::string>& requested_ui_root,
      AuthSupportService& auth_support,
      const AssignmentOrchestrationService& assignment_orchestration_service,
      const BundleCliService& bundle_cli_service) const;
};

}  // namespace comet::controller
