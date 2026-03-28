#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "app/controller_service_interfaces.h"
#include "scheduler/scheduler_view_service.h"
#include "read_model/state_aggregate_loader.h"

#include "comet/models.h"
#include "comet/sqlite_store.h"

namespace comet::controller {

class ReadModelCliService : public IReadModelCliService {
 public:
  using FilterHostObservationsForPlaneFn =
      std::function<std::vector<comet::HostObservation>(
          const std::vector<comet::HostObservation>&,
          const std::string&)>;
  using PrintHostAssignmentsFn =
      std::function<void(const std::vector<comet::HostAssignment>&)>;
  using PrintHostObservationsFn =
      std::function<void(const std::vector<comet::HostObservation>&, int)>;
  using PrintHostHealthFn = std::function<void(
      const std::optional<comet::DesiredState>&,
      const std::vector<comet::HostObservation>&,
      const std::vector<comet::NodeAvailabilityOverride>&,
      const std::optional<std::string>&,
      int)>;
  using PrintEventsFn =
      std::function<void(const std::vector<comet::EventRecord>&)>;
  using PrintNodeAvailabilityOverridesFn =
      std::function<void(const std::vector<comet::NodeAvailabilityOverride>&)>;
  using PrintStateSummaryFn =
      std::function<void(const comet::DesiredState&)>;
  using PrintDiskRuntimeStatesFn =
      std::function<void(const std::vector<comet::DiskRuntimeState>&)>;
  using PrintDetailedDiskStateFn = std::function<void(
      const comet::DesiredState&,
      const std::vector<comet::DiskRuntimeState>&,
      const std::vector<comet::HostObservation>&,
      const std::optional<std::string>&)>;
  using PrintSchedulerDecisionSummaryFn =
      std::function<void(const comet::DesiredState&)>;
  using PrintRolloutGateSummaryFn =
      std::function<void(const comet::SchedulingPolicyReport&)>;
  using VerificationStableSamplesRequiredFn = std::function<int()>;
  using DefaultStaleAfterSecondsFn = std::function<int()>;

  struct Deps {
    FilterHostObservationsForPlaneFn filter_host_observations_for_plane;
    PrintHostAssignmentsFn print_host_assignments;
    PrintHostObservationsFn print_host_observations;
    PrintHostHealthFn print_host_health;
    PrintEventsFn print_events;
    PrintNodeAvailabilityOverridesFn print_node_availability_overrides;
    PrintStateSummaryFn print_state_summary;
    PrintDiskRuntimeStatesFn print_disk_runtime_states;
    PrintDetailedDiskStateFn print_detailed_disk_state;
    PrintSchedulerDecisionSummaryFn print_scheduler_decision_summary;
    PrintRolloutGateSummaryFn print_rollout_gate_summary;
    VerificationStableSamplesRequiredFn verification_stable_samples_required;
    DefaultStaleAfterSecondsFn default_stale_after_seconds;
    const StateAggregateLoader* state_aggregate_loader = nullptr;
    const SchedulerViewService* scheduler_view_service = nullptr;
  };

  explicit ReadModelCliService(Deps deps);

  int ShowHostAssignments(
      const std::string& db_path,
      const std::optional<std::string>& node_name) const override;

  int ShowHostObservations(
      const std::string& db_path,
      const std::optional<std::string>& plane_name,
      const std::optional<std::string>& node_name,
      int stale_after_seconds) const override;

  int ShowNodeAvailability(
      const std::string& db_path,
      const std::optional<std::string>& node_name) const override;

  int ShowHostHealth(
      const std::string& db_path,
      const std::optional<std::string>& node_name,
      int stale_after_seconds) const override;

  int ShowEvents(
      const std::string& db_path,
      const std::optional<std::string>& plane_name,
      const std::optional<std::string>& node_name,
      const std::optional<std::string>& worker_name,
      const std::optional<std::string>& category,
      int limit) const override;

  int ShowState(const std::string& db_path) const override;

  int ShowDiskState(
      const std::string& db_path,
      const std::optional<std::string>& node_name,
      const std::optional<std::string>& plane_name) const override;

 private:
  Deps deps_;
};

}  // namespace comet::controller
