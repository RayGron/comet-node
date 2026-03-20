#pragma once

#include <optional>
#include <string>
#include <vector>

#include "comet/models.h"

namespace comet {

enum class HostAssignmentStatus {
  Pending,
  Claimed,
  Applied,
  Failed,
  Superseded,
};

enum class HostObservationStatus {
  Idle,
  Applying,
  Applied,
  Failed,
};

enum class NodeAvailability {
  Active,
  Draining,
  Unavailable,
};

struct HostAssignment {
  int id = 0;
  std::string node_name;
  std::string plane_name;
  int desired_generation = 0;
  int attempt_count = 0;
  int max_attempts = 3;
  std::string assignment_type;
  std::string desired_state_json;
  std::string artifacts_root;
  HostAssignmentStatus status = HostAssignmentStatus::Pending;
  std::string status_message;
};

struct HostObservation {
  std::string node_name;
  std::string plane_name;
  std::optional<int> applied_generation;
  std::optional<int> last_assignment_id;
  HostObservationStatus status = HostObservationStatus::Idle;
  std::string status_message;
  std::string observed_state_json;
  std::string runtime_status_json;
  std::string heartbeat_at;
};

struct NodeAvailabilityOverride {
  std::string node_name;
  NodeAvailability availability = NodeAvailability::Active;
  std::string status_message;
  std::string updated_at;
};

class ControllerStore {
 public:
  explicit ControllerStore(std::string db_path);
  ~ControllerStore();

  ControllerStore(const ControllerStore&) = delete;
  ControllerStore& operator=(const ControllerStore&) = delete;

  void Initialize();
  void ReplaceDesiredState(const DesiredState& state, int generation);
  void ReplaceDesiredState(const DesiredState& state);
  std::optional<DesiredState> LoadDesiredState() const;
  std::optional<int> LoadDesiredGeneration() const;
  void UpsertNodeAvailabilityOverride(const NodeAvailabilityOverride& availability_override);
  std::optional<NodeAvailabilityOverride> LoadNodeAvailabilityOverride(
      const std::string& node_name) const;
  std::vector<NodeAvailabilityOverride> LoadNodeAvailabilityOverrides(
      const std::optional<std::string>& node_name = std::nullopt) const;
  void UpsertHostObservation(const HostObservation& observation);
  std::optional<HostObservation> LoadHostObservation(const std::string& node_name) const;
  std::vector<HostObservation> LoadHostObservations(
      const std::optional<std::string>& node_name = std::nullopt) const;
  void ReplaceHostAssignments(const std::vector<HostAssignment>& assignments);
  void EnqueueHostAssignments(
      const std::vector<HostAssignment>& assignments,
      const std::string& supersede_reason = "");
  std::optional<HostAssignment> LoadHostAssignment(int assignment_id) const;
  std::vector<HostAssignment> LoadHostAssignments(
      const std::optional<std::string>& node_name = std::nullopt,
      const std::optional<HostAssignmentStatus>& status = std::nullopt) const;
  std::optional<HostAssignment> ClaimNextHostAssignment(const std::string& node_name);
  bool TransitionClaimedHostAssignment(
      int assignment_id,
      HostAssignmentStatus status,
      const std::string& status_message = "");
  bool RetryFailedHostAssignment(
      int assignment_id,
      const std::string& status_message = "");
  void UpdateHostAssignmentStatus(
      int assignment_id,
      HostAssignmentStatus status,
      const std::string& status_message = "");

  const std::string& db_path() const;

 private:
  std::string db_path_;
  void* db_ = nullptr;
};

std::string ToString(HostAssignmentStatus status);
HostAssignmentStatus ParseHostAssignmentStatus(const std::string& value);
std::string ToString(HostObservationStatus status);
HostObservationStatus ParseHostObservationStatus(const std::string& value);
std::string ToString(NodeAvailability availability);
NodeAvailability ParseNodeAvailability(const std::string& value);

}  // namespace comet
