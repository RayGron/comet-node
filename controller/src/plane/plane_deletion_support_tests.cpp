#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "comet/state/sqlite_store.h"
#include "host/host_assignment_reconciliation_service.h"
#include "plane/controller_state_service.h"
#include "plane/plane_lifecycle_support.h"
#include "plane/plane_deletion_support.h"
#include "plane/plane_service.h"
#include "plane/plane_state_presentation_support.h"

namespace fs = std::filesystem;

namespace {

void Expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

comet::DesiredState BuildDesiredState(
    const std::string& plane_name,
    const std::vector<std::string>& node_names = {}) {
  comet::DesiredState state;
  state.plane_name = plane_name;
  state.plane_mode = comet::PlaneMode::Llm;
  state.control_root = "/tmp/" + plane_name;
  for (const auto& node_name : node_names) {
    comet::NodeInventory node;
    node.name = node_name;
    state.nodes.push_back(node);
  }
  return state;
}

comet::HostAssignment BuildHostAssignment(
    const std::string& plane_name,
    const std::string& node_name,
    int desired_generation,
    comet::HostAssignmentStatus status,
    const std::string& assignment_type = "apply-node-state") {
  comet::HostAssignment assignment;
  assignment.node_name = node_name;
  assignment.plane_name = plane_name;
  assignment.desired_generation = desired_generation;
  assignment.assignment_type = assignment_type;
  assignment.desired_state_json = "{}";
  assignment.artifacts_root = "/tmp/artifacts";
  assignment.status = status;
  assignment.attempt_count = status == comet::HostAssignmentStatus::Claimed ? 1 : 0;
  return assignment;
}

comet::HostObservation BuildHostObservation(
    const std::string& plane_name,
    const std::string& node_name,
    int applied_generation,
    comet::HostObservationStatus status = comet::HostObservationStatus::Applied) {
  comet::HostObservation observation;
  observation.node_name = node_name;
  observation.plane_name = plane_name;
  observation.applied_generation = applied_generation;
  observation.status = status;
  return observation;
}

class TestPlaneStatePresentationSupport final
    : public comet::controller::PlaneStatePresentationSupport {
 public:
  std::string FormatTimestamp(const std::string& value) const override { return value; }

  void PrintStateSummary(const comet::DesiredState&) const override {}
};

class TestPlaneLifecycleSupport final : public comet::controller::PlaneLifecycleSupport {
 public:
  void PrepareDesiredState(
      comet::ControllerStore&,
      comet::DesiredState*) const override {}

  void AppendPlaneEvent(
      comet::ControllerStore&,
      const std::string&,
      const std::string&,
      const nlohmann::json&,
      const std::string&) const override {}

  bool CanFinalizeDeletedPlane(
      comet::ControllerStore&,
      const std::string&) const override {
    return true;
  }

  std::optional<comet::HostAssignment> FindLatestHostAssignmentForPlane(
      const std::vector<comet::HostAssignment>&,
      const std::string&) const override {
    return std::nullopt;
  }

  std::vector<comet::HostAssignment> BuildStartAssignments(
      const comet::DesiredState&,
      const std::string&,
      int,
      const std::vector<comet::NodeAvailabilityOverride>&,
      const std::vector<comet::HostObservation>&,
      const comet::SchedulingPolicyReport&) const override {
    return {};
  }

  std::vector<comet::HostAssignment> BuildStopAssignments(
      const comet::DesiredState&,
      int,
      const std::string&,
      const std::vector<comet::NodeAvailabilityOverride>&) const override {
    return {};
  }

  std::vector<comet::HostAssignment> BuildDeleteAssignments(
      const comet::DesiredState&,
      int,
      const std::string&) const override {
    return {};
  }

  std::string DefaultArtifactsRoot() const override { return "/tmp/artifacts"; }
};

comet::controller::PlaneService BuildPlaneService(const std::string& db_path) {
  auto state_presentation_support =
      std::make_shared<TestPlaneStatePresentationSupport>();
  auto lifecycle_support = std::make_shared<TestPlaneLifecycleSupport>();
  return comet::controller::PlaneService(
      db_path,
      std::move(state_presentation_support),
      std::move(lifecycle_support));
}

}  // namespace

int main() {
  try {
    const fs::path db_path = fs::temp_directory_path() / "comet-plane-deletion-tests.sqlite";
    std::error_code error;
    fs::remove(db_path, error);

    {
      comet::ControllerStore store(db_path.string());
      store.Initialize();
      store.ReplaceDesiredState(BuildDesiredState("plane-a"), 7);
      Expect(store.UpdatePlaneState("plane-a", "deleting"), "plane-a should enter deleting");

      comet::controller::ControllerStateService state_service(
          comet::controller::ControllerStateService::Deps{
              [](comet::ControllerStore&, const std::string&) { return true; },
              [](comet::ControllerStore&,
                 const std::string&,
                 const std::string&,
                 const std::string&,
                 const nlohmann::json&,
                 const std::string&) {},
          });
      const auto payload = state_service.BuildPayload(db_path.string(), std::nullopt);
      Expect(payload.at("planes").is_array(), "planes payload should be an array");
      Expect(payload.at("planes").empty(), "deleting plane should be finalized on state read");
    }

    {
      comet::ControllerStore store(db_path.string());
      store.Initialize();
      store.ReplaceDesiredState(BuildDesiredState("plane-b"), 8);
      Expect(store.UpdatePlaneState("plane-b", "deleting"), "plane-b should enter deleting");

      auto plane_service = BuildPlaneService(db_path.string());
      bool threw_not_found = false;
      try {
        (void)plane_service.ShowPlane("plane-b");
      } catch (const std::exception& ex) {
        threw_not_found = std::string(ex.what()).find("not found") != std::string::npos;
      }
      Expect(threw_not_found, "show-plane should finalize deleted plane before reading it");
      Expect(!store.LoadPlane("plane-b").has_value(), "plane-b should be removed from store");
    }

    {
      comet::ControllerStore store(db_path.string());
      store.Initialize();
      const comet::controller::HostAssignmentReconciliationService reconciliation_service;

      store.ReplaceDesiredState(BuildDesiredState("plane-c", {"node-c"}), 4);
      Expect(
          store.UpdatePlaneAppliedGeneration("plane-c", 4),
          "plane-c applied generation should update");
      store.UpsertHostObservation(BuildHostObservation("plane-c", "node-c", 4));
      store.ReplaceHostAssignments(
          {BuildHostAssignment(
              "plane-c",
              "node-c",
              4,
              comet::HostAssignmentStatus::Claimed)});

      const auto result = reconciliation_service.Reconcile(store, "plane-c");
      const auto assignment = store.LoadHostAssignments(
          std::nullopt, std::nullopt, "plane-c");
      Expect(result.applied == 1, "controller should mark converged claimed assignment applied");
      Expect(result.superseded == 0, "controller should not supersede converged assignment");
      Expect(assignment.size() == 1, "plane-c should have one assignment");
      Expect(
          assignment.front().status == comet::HostAssignmentStatus::Applied,
          "plane-c claimed assignment should become applied");
    }

    {
      comet::ControllerStore store(db_path.string());
      store.Initialize();
      const comet::controller::HostAssignmentReconciliationService reconciliation_service;

      store.ReplaceDesiredState(BuildDesiredState("plane-d", {"node-d"}), 6);
      store.ReplaceHostAssignments(
          {BuildHostAssignment(
               "plane-d",
               "node-d",
               5,
               comet::HostAssignmentStatus::Claimed),
           BuildHostAssignment(
               "plane-d",
               "node-d",
               6,
               comet::HostAssignmentStatus::Pending)});

      const auto result = reconciliation_service.Reconcile(store, "plane-d");
      const auto assignments = store.LoadHostAssignments(
          std::nullopt, std::nullopt, "plane-d");
      Expect(result.superseded == 1, "controller should supersede replaced claimed assignment");
      Expect(result.applied == 0, "controller should not mark replaced assignment applied");
      Expect(assignments.size() == 2, "plane-d should have two assignments");
      Expect(
          assignments.front().status == comet::HostAssignmentStatus::Superseded,
          "older claimed assignment should be superseded");
      Expect(
          assignments.back().status == comet::HostAssignmentStatus::Pending,
          "newest assignment should remain pending");
    }

    {
      comet::ControllerStore store(db_path.string());
      store.Initialize();
      const comet::controller::HostAssignmentReconciliationService reconciliation_service;

      store.ReplaceDesiredState(BuildDesiredState("plane-e", {"node-e"}), 5);
      Expect(
          store.UpdatePlaneAppliedGeneration("plane-e", 4),
          "plane-e applied generation should update");
      store.UpsertHostObservation(BuildHostObservation("plane-e", "node-e", 4));
      store.ReplaceHostAssignments(
          {BuildHostAssignment(
              "plane-e",
              "node-e",
              5,
              comet::HostAssignmentStatus::Claimed)});

      const auto result = reconciliation_service.Reconcile(store, "plane-e");
      const auto assignment = store.LoadHostAssignments(
          std::nullopt, std::nullopt, "plane-e");
      Expect(result.Total() == 0, "controller should not reconcile unconverged assignment");
      Expect(assignment.size() == 1, "plane-e should have one assignment");
      Expect(
          assignment.front().status == comet::HostAssignmentStatus::Claimed,
          "unconverged assignment should remain claimed");
    }

    fs::remove(db_path, error);
    std::cout << "controller plane deletion support tests passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << "\n";
    return 1;
  }
}
