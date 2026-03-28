#pragma once

#include <optional>
#include <string>
#include <vector>

#include "http/controller_http_transport.h"

#include "comet/state/models.h"
#include "comet/runtime/runtime_status.h"
#include "comet/state/sqlite_store.h"

namespace comet::controller {

class InteractionRuntimeSupportService {
 public:
  std::optional<ControllerEndpointTarget> ParseInteractionTarget(
      const std::string& gateway_listen,
      int fallback_port) const;

  std::optional<std::string> FindInferInstanceName(
      const comet::DesiredState& desired_state) const;

  std::vector<std::string> FindWorkerInstanceNames(
      const comet::DesiredState& desired_state) const;

  std::optional<comet::RuntimeProcessStatus> FindInstanceRuntimeStatus(
      const std::vector<comet::RuntimeProcessStatus>& statuses,
      const std::string& instance_name) const;

  bool ProbeControllerTargetOk(
      const std::optional<ControllerEndpointTarget>& target,
      const std::string& path) const;

  std::optional<comet::RuntimeStatus> BuildPlaneScopedRuntimeStatus(
      const comet::DesiredState& desired_state,
      const comet::HostObservation& observation,
      const std::function<std::vector<comet::RuntimeProcessStatus>(
          const comet::HostObservation&)>& parse_instance_runtime_statuses) const;

  int CountReadyWorkerMembers(
      comet::ControllerStore& store,
      const comet::DesiredState& desired_state,
      const std::function<std::vector<comet::RuntimeProcessStatus>(
          const comet::HostObservation&)>& parse_instance_runtime_statuses) const;
};

}  // namespace comet::controller
