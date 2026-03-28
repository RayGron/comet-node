#include "backend/local_db_hostd_backend.h"

namespace comet::hostd {

LocalDbHostdBackend::LocalDbHostdBackend(std::string db_path) : store_(std::move(db_path)) {
  store_.Initialize();
}

std::optional<comet::HostAssignment> LocalDbHostdBackend::ClaimNextHostAssignment(
    const std::string& node_name) {
  return store_.ClaimNextHostAssignment(node_name);
}

bool LocalDbHostdBackend::TransitionClaimedHostAssignment(
    const int assignment_id,
    const comet::HostAssignmentStatus status,
    const std::string& status_message) {
  return store_.TransitionClaimedHostAssignment(assignment_id, status, status_message);
}

bool LocalDbHostdBackend::UpdateHostAssignmentProgress(
    const int assignment_id,
    const nlohmann::json& progress) {
  return store_.UpdateHostAssignmentProgress(assignment_id, progress.dump());
}

void LocalDbHostdBackend::UpsertHostObservation(const comet::HostObservation& observation) {
  store_.UpsertHostObservation(observation);
}

void LocalDbHostdBackend::AppendEvent(const comet::EventRecord& event) {
  store_.AppendEvent(event);
}

void LocalDbHostdBackend::UpsertDiskRuntimeState(const comet::DiskRuntimeState& state) {
  store_.UpsertDiskRuntimeState(state);
}

std::optional<comet::DiskRuntimeState> LocalDbHostdBackend::LoadDiskRuntimeState(
    const std::string& disk_name,
    const std::string& node_name) {
  return store_.LoadDiskRuntimeState(disk_name, node_name);
}

}  // namespace comet::hostd
