#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "infra/controller_action.h"
#include "app/controller_service_interfaces.h"

#include "comet/models.h"
#include "comet/sqlite_store.h"

namespace comet::controller {

class AssignmentOrchestrationService : public IAssignmentOrchestrationService {
 public:
  using DefaultArtifactsRootProvider = std::function<std::string()>;
  using EventAppender = std::function<void(
      comet::ControllerStore&,
      const std::string&,
      const std::string&,
      const std::string&,
      const nlohmann::json&,
      const std::string&,
      const std::string&,
      const std::string&,
      const std::optional<int>&)>;
  using PrintNodeAvailabilityOverridesFn =
      std::function<void(const std::vector<comet::NodeAvailabilityOverride>&)>;
  using PrintHostAssignmentsFn =
      std::function<void(const std::vector<comet::HostAssignment>&)>;

  struct Deps {
    DefaultArtifactsRootProvider default_artifacts_root_provider;
    EventAppender event_appender;
    PrintNodeAvailabilityOverridesFn print_node_availability_overrides;
    PrintHostAssignmentsFn print_host_assignments;
  };

  explicit AssignmentOrchestrationService(Deps deps);

  std::optional<comet::HostAssignment> BuildResyncAssignmentForNode(
      const comet::DesiredState& desired_state,
      int desired_generation,
      const std::string& node_name,
      const std::vector<comet::HostAssignment>& existing_assignments,
      const std::optional<comet::HostObservation>& observation) const;

  std::optional<comet::HostAssignment> BuildDrainAssignmentForNode(
      const comet::DesiredState& desired_state,
      int desired_generation,
      const std::string& node_name,
      const std::vector<comet::HostAssignment>& existing_assignments) const;

  int SetNodeAvailability(
      const std::string& db_path,
      const std::string& node_name,
      comet::NodeAvailability availability,
      const std::optional<std::string>& status_message) const override;

  int RetryHostAssignment(
      const std::string& db_path,
      int assignment_id) const override;

  ControllerActionResult ExecuteSetNodeAvailabilityAction(
      const std::string& db_path,
      const std::string& node_name,
      comet::NodeAvailability availability,
      const std::optional<std::string>& status_message) const;

  ControllerActionResult ExecuteRetryHostAssignmentAction(
      const std::string& db_path,
      int assignment_id) const override;

 private:
  Deps deps_;
};

}  // namespace comet::controller
