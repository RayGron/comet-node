#include "app/hostd_app_assignment_support.h"

namespace comet::hostd {

HostdAppAssignmentSupport::HostdAppAssignmentSupport()
    : path_support_(),
      runtime_telemetry_support_(),
      local_state_path_support_(),
      local_state_repository_(local_state_path_support_),
      local_runtime_state_support_(
          path_support_,
          local_state_repository_,
          runtime_telemetry_support_),
      command_support_(),
      file_support_(),
      compose_runtime_support_(command_support_),
      disk_runtime_support_(command_support_, path_support_, file_support_),
      apply_plan_support_(
          command_support_,
          compose_runtime_support_,
          disk_runtime_support_,
          file_support_),
      post_deploy_support_(command_support_),
      reporting_support_(),
      bootstrap_model_support_factory_(
          path_support_,
          command_support_,
          file_support_,
          reporting_support_),
      bootstrap_model_support_(bootstrap_model_support_factory_.Create()),
      display_support_(path_support_),
      apply_support_(
          path_support_,
          display_support_,
          apply_plan_support_,
          disk_runtime_support_,
          post_deploy_support_,
          local_state_repository_,
          local_runtime_state_support_,
          bootstrap_model_support_),
      observation_support_() {}

comet::DesiredState HostdAppAssignmentSupport::RebaseStateForRuntimeRoot(
    comet::DesiredState state,
    const std::string& storage_root,
    const std::optional<std::string>& runtime_root) const {
  return path_support_.RebaseStateForRuntimeRoot(std::move(state), storage_root, runtime_root);
}

nlohmann::json HostdAppAssignmentSupport::BuildAssignmentProgressPayload(
    const std::string& phase,
    const std::string& phase_label,
    const std::string& message,
    int progress_percent,
    const std::string& plane_name,
    const std::string& node_name) const {
  return reporting_support_.BuildAssignmentProgressPayload(
      phase,
      phase_label,
      message,
      progress_percent,
      plane_name,
      node_name);
}

void HostdAppAssignmentSupport::PublishAssignmentProgress(
    HostdBackend* backend,
    const std::optional<int>& assignment_id,
    const nlohmann::json& progress) const {
  reporting_support_.PublishAssignmentProgress(backend, assignment_id, progress);
}

std::vector<std::string> HostdAppAssignmentSupport::ParseTaggedCsv(
    const std::string& tagged_message,
    const std::string& tag) const {
  return reporting_support_.ParseTaggedCsv(tagged_message, tag);
}

comet::HostObservation HostdAppAssignmentSupport::BuildObservedStateSnapshot(
    const std::string& node_name,
    const std::string& storage_root,
    const std::string& state_root,
    comet::HostObservationStatus status,
    const std::string& status_message,
    const std::optional<int>& assignment_id) const {
  return observation_support_.BuildObservedStateSnapshot(
      node_name,
      storage_root,
      state_root,
      status,
      status_message,
      assignment_id);
}

std::map<std::string, int> HostdAppAssignmentSupport::CaptureServiceHostPids(
    const std::vector<std::string>& service_names) const {
  return reporting_support_.CaptureServiceHostPids(service_names);
}

bool HostdAppAssignmentSupport::VerifyEvictionAssignment(
    const comet::DesiredState& desired_state,
    const std::string& node_name,
    const std::string& state_root,
    const std::string& tagged_message,
    const std::map<std::string, int>& expected_victim_host_pids) const {
  return reporting_support_.VerifyEvictionAssignment(
      desired_state,
      node_name,
      state_root,
      tagged_message,
      expected_victim_host_pids);
}

void HostdAppAssignmentSupport::ApplyDesiredNodeState(
    const comet::DesiredState& desired_node_state,
    const std::string& artifacts_root,
    const std::string& storage_root,
    const std::optional<std::string>& runtime_root,
    const std::string& state_root,
    ComposeMode compose_mode,
    const std::string& source_label,
    const std::optional<int>& desired_generation,
    const std::optional<int>& assignment_id,
    HostdBackend* backend) const {
  apply_support_.ApplyDesiredNodeState(
      desired_node_state,
      artifacts_root,
      storage_root,
      runtime_root,
      state_root,
      compose_mode,
      source_label,
      desired_generation,
      assignment_id,
      backend,
      [&](const std::string& phase,
          const std::string& title,
          const std::string& detail,
          int percent,
          const std::string& plane_name,
          const std::string& node_name) {
        PublishAssignmentProgress(
            backend,
            assignment_id,
            BuildAssignmentProgressPayload(
                phase,
                title,
                detail,
                percent,
                plane_name,
                node_name));
      });
}

void HostdAppAssignmentSupport::ShowDemoOps(
    const std::string& node_name,
    const std::string& storage_root,
    const std::optional<std::string>& runtime_root) const {
  display_support_.ShowDemoOps(node_name, storage_root, runtime_root);
}

void HostdAppAssignmentSupport::ShowStateOps(
    const std::string& db_path,
    const std::string& node_name,
    const std::string& artifacts_root,
    const std::string& storage_root,
    const std::optional<std::string>& runtime_root,
    const std::string& state_root) const {
  display_support_.ShowStateOps(
      db_path,
      node_name,
      artifacts_root,
      storage_root,
      runtime_root,
      state_root);
}

void HostdAppAssignmentSupport::AppendHostdEvent(
    HostdBackend& backend,
    const std::string& category,
    const std::string& event_type,
    const std::string& message,
    const nlohmann::json& payload,
    const std::string& plane_name,
    const std::string& node_name,
    const std::string& worker_name,
    const std::optional<int>& assignment_id,
    const std::optional<int>& rollout_action_id,
    const std::string& severity) const {
  observation_support_.AppendHostdEvent(
      backend,
      category,
      event_type,
      message,
      payload,
      plane_name,
      node_name,
      worker_name,
      assignment_id,
      rollout_action_id,
      severity);
}

}  // namespace comet::hostd
