#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "comet/state/sqlite_store.h"

namespace comet::controller {

class HostAssignmentReconciliationService {
 public:
  struct Result {
    int applied = 0;
    int superseded = 0;

    int Total() const {
      return applied + superseded;
    }
  };

  Result Reconcile(
      comet::ControllerStore& store,
      const std::optional<std::string>& plane_name = std::nullopt) const;

 private:
  Result ReconcilePlane(
      comet::ControllerStore& store,
      const std::string& plane_name,
      const std::vector<comet::HostAssignment>& claimed_assignments,
      const std::vector<comet::HostObservation>& observations) const;
  std::vector<comet::HostAssignment> LoadClaimedApplyAssignments(
      comet::ControllerStore& store,
      const std::optional<std::string>& plane_name) const;
  std::vector<std::string> BuildPlaneNames(
      const std::vector<comet::HostAssignment>& claimed_assignments) const;
  std::map<std::string, comet::HostAssignment> BuildLatestAssignmentsByNode(
      const std::vector<comet::HostAssignment>& assignments) const;
  std::optional<comet::HostObservation> FindObservationForNode(
      const std::vector<comet::HostObservation>& observations,
      const std::string& node_name) const;
  bool ShouldSupersedeClaimedAssignment(
      const comet::HostAssignment& assignment,
      const std::map<std::string, comet::HostAssignment>& latest_assignments_by_node) const;
  bool ShouldMarkClaimedAssignmentApplied(
      const comet::HostAssignment& assignment,
      const std::optional<comet::PlaneRecord>& plane,
      const std::optional<comet::HostObservation>& observation) const;
};

}  // namespace comet::controller
