#pragma once

#include <optional>
#include <string>
#include <vector>

#include "infra/controller_runtime_support_service.h"
#include "comet/state/models.h"
#include "comet/runtime/runtime_status.h"
#include "comet/state/sqlite_store.h"

namespace comet::controller {

class ControllerPrintService {
 public:
  ControllerPrintService();
  explicit ControllerPrintService(ControllerRuntimeSupportService runtime_support_service);

  void PrintStateSummary(const comet::DesiredState& state) const;
  void PrintDiskRuntimeStates(const std::vector<comet::DiskRuntimeState>& runtime_states) const;
  void PrintDetailedDiskState(
      const comet::DesiredState& state,
      const std::vector<comet::DiskRuntimeState>& runtime_states,
      const std::vector<comet::HostObservation>& observations,
      const std::optional<std::string>& node_name) const;
  void PrintSchedulerDecisionSummary(const comet::DesiredState& state) const;
  void PrintRolloutGateSummary(const comet::SchedulingPolicyReport& scheduling_report) const;
  void PrintPersistedRolloutActions(
      const std::vector<comet::RolloutActionRecord>& actions) const;
  void PrintNodeAvailabilityOverrides(
      const std::vector<comet::NodeAvailabilityOverride>& availability_overrides) const;
  void PrintAssignmentDispatchSummary(
      const comet::DesiredState& desired_state,
      const std::map<std::string, comet::NodeAvailabilityOverride>& availability_overrides,
      const std::vector<comet::HostObservation>& observations,
      int stale_after_seconds) const;
  void PrintHostAssignments(
      const std::vector<comet::HostAssignment>& assignments) const;
  void PrintHostObservations(
      const std::vector<comet::HostObservation>& observations,
      int stale_after_seconds) const;
  void PrintHostHealth(
      const std::optional<comet::DesiredState>& desired_state,
      const std::vector<comet::HostObservation>& observations,
      const std::vector<comet::NodeAvailabilityOverride>& availability_overrides,
      const std::optional<std::string>& node_name,
      int stale_after_seconds) const;
  void PrintEvents(const std::vector<comet::EventRecord>& events) const;

 private:
  bool IsNodeSchedulable(comet::NodeAvailability availability) const;
  std::optional<std::string> ObservedSchedulingGateReason(
      const std::vector<comet::HostObservation>& observations,
      const std::string& node_name,
      int stale_after_seconds) const;
  std::optional<std::tm> ParseDisplayTimestamp(const std::string& value) const;
  std::string FormatDisplayTimestamp(const std::string& value) const;

  ControllerRuntimeSupportService runtime_support_service_;
};

}  // namespace comet::controller
