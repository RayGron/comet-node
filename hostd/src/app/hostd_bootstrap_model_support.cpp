#include "app/hostd_bootstrap_model_support.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <thread>

namespace comet::hostd {

namespace fs = std::filesystem;

HostdBootstrapModelSupport::HostdBootstrapModelSupport(
    const HostdBootstrapModelArtifactSupport& artifact_support,
    const HostdBootstrapActiveModelSupport& active_model_support,
    const HostdBootstrapTransferSupport& transfer_support,
    const HostdFileSupport& file_support,
    const HostdReportingSupport& reporting_support)
    : artifact_support_(artifact_support),
      active_model_support_(active_model_support),
      transfer_support_(transfer_support),
      file_support_(file_support),
      reporting_support_(reporting_support) {}

void HostdBootstrapModelSupport::PublishAssignmentProgress(
    HostdBackend* backend,
    const std::optional<int>& assignment_id,
    const std::string& phase,
    const std::string& title,
    const std::string& detail,
    int percent,
    const std::string& plane_name,
    const std::string& node_name,
    const std::optional<std::uintmax_t>& bytes_done,
    const std::optional<std::uintmax_t>& bytes_total) const {
  reporting_support_.PublishAssignmentProgress(
      backend,
      assignment_id,
      reporting_support_.BuildAssignmentProgressPayload(
          phase,
          title,
          detail,
          percent,
          plane_name,
          node_name,
          bytes_done,
          bytes_total));
}

bool HostdBootstrapModelSupport::TryUseReferenceBootstrapModel(
    const comet::DesiredState& state,
    const std::string& node_name,
    const comet::BootstrapModelSpec& bootstrap_model,
    HostdBackend* backend,
    const std::optional<int>& assignment_id) const {
  if (bootstrap_model.materialization_mode != "reference" ||
      !bootstrap_model.local_path.has_value() ||
      bootstrap_model.local_path->empty()) {
    return false;
  }

  std::error_code error;
  if (!fs::exists(*bootstrap_model.local_path, error) || error) {
    throw std::runtime_error(
        "bootstrap model reference path does not exist: " + *bootstrap_model.local_path);
  }

  PublishAssignmentProgress(
      backend,
      assignment_id,
      "using-model-reference",
      "Using model reference",
      "Using the configured model path directly without copying it into the plane shared disk.",
      72,
      state.plane_name,
      node_name);
  active_model_support_.WriteBootstrapActiveModel(
      state,
      node_name,
      *bootstrap_model.local_path,
      *bootstrap_model.local_path);
  return true;
}

bool HostdBootstrapModelSupport::TryUseSharedBootstrapFromOtherNode(
    const comet::DesiredState& state,
    const std::string& node_name,
    const std::vector<HostdBootstrapModelArtifact>& artifacts,
    const std::string& target_path,
    HostdBackend* backend,
    const std::optional<int>& assignment_id) const {
  const std::string bootstrap_owner_node = artifact_support_.SharedModelBootstrapOwnerNode(state);
  const bool shared_bootstrap_owned_elsewhere =
      bootstrap_owner_node != node_name &&
      std::any_of(
          artifacts.begin(),
          artifacts.end(),
          [](const HostdBootstrapModelArtifact& artifact) {
            return artifact.local_path.has_value() && !artifact.local_path->empty();
          });
  if (!shared_bootstrap_owned_elsewhere) {
    return false;
  }

  for (int attempt = 0; attempt < 300; ++attempt) {
    if (HostdBootstrapModelArtifactSupport::LooksLikeRecognizedModelDirectory(target_path) ||
        fs::exists(target_path)) {
      active_model_support_.WriteBootstrapActiveModel(state, node_name, target_path);
      return true;
    }
    PublishAssignmentProgress(
        backend,
        assignment_id,
        "waiting-for-model",
        "Waiting for shared model",
        "Waiting for " + bootstrap_owner_node +
            " to finish copying the shared model into the plane disk.",
        18,
        state.plane_name,
        node_name);
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  throw std::runtime_error(
      "timed out waiting for shared model bootstrap on node '" + bootstrap_owner_node + "'");
}

std::optional<std::uintmax_t> HostdBootstrapModelSupport::ExpectedArtifactSize(
    const HostdBootstrapModelArtifact& artifact) const {
  if (artifact.local_path.has_value() && !artifact.local_path->empty()) {
    return transfer_support_.FileSizeIfExists(*artifact.local_path);
  }
  if (artifact.source_url.has_value() && !artifact.source_url->empty()) {
    return transfer_support_.ProbeContentLength(*artifact.source_url);
  }
  return std::nullopt;
}

bool HostdBootstrapModelSupport::IsArtifactAlreadyPresent(
    const HostdBootstrapModelArtifact& artifact) const {
  if (!fs::exists(artifact.target_host_path)) {
    return false;
  }

  const auto target_size = transfer_support_.FileSizeIfExists(artifact.target_host_path);
  if (artifact.local_path.has_value() && !artifact.local_path->empty()) {
    const auto source_size = transfer_support_.FileSizeIfExists(*artifact.local_path);
    const bool source_is_directory = fs::is_directory(*artifact.local_path);
    const bool target_has_model_root =
        !source_is_directory ||
        HostdBootstrapModelArtifactSupport::LooksLikeRecognizedModelDirectory(
            artifact.target_host_path);
    return source_size.has_value() && target_size.has_value() &&
           *source_size == *target_size && target_has_model_root;
  }
  if (artifact.source_url.has_value() && !artifact.source_url->empty()) {
    const auto remote_size = transfer_support_.ProbeContentLength(*artifact.source_url);
    return remote_size.has_value() && target_size.has_value() && *remote_size == *target_size;
  }
  return fs::exists(artifact.target_host_path);
}

std::optional<std::uintmax_t> HostdBootstrapModelSupport::ComputeAggregateExpectedSize(
    const std::vector<HostdBootstrapModelArtifact>& artifacts,
    bool& already_present) const {
  already_present = !artifacts.empty();
  std::optional<std::uintmax_t> aggregate_total = std::uintmax_t{0};
  for (const auto& artifact : artifacts) {
    if (!IsArtifactAlreadyPresent(artifact)) {
      already_present = false;
    }
    const auto expected_size = ExpectedArtifactSize(artifact);
    if (!expected_size.has_value()) {
      aggregate_total = std::nullopt;
    } else if (aggregate_total.has_value()) {
      *aggregate_total += *expected_size;
    }
  }
  return aggregate_total;
}

void HostdBootstrapModelSupport::AcquireArtifactsIfNeeded(
    const comet::DesiredState& state,
    const std::string& node_name,
    const std::vector<HostdBootstrapModelArtifact>& artifacts,
    const std::optional<std::uintmax_t>& aggregate_total,
    bool already_present,
    HostdBackend* backend,
    const std::optional<int>& assignment_id) const {
  if (already_present) {
    return;
  }

  std::uintmax_t aggregate_prefix = 0;
  for (std::size_t index = 0; index < artifacts.size(); ++index) {
    const auto& artifact = artifacts[index];
    std::optional<std::uintmax_t> artifact_size =
        transfer_support_.FileSizeIfExists(artifact.target_host_path);
    if (artifact.local_path.has_value() && !artifact.local_path->empty()) {
      if (!IsArtifactAlreadyPresent(artifact)) {
        transfer_support_.CopyFileWithProgress(
            *artifact.local_path,
            artifact.target_host_path,
            backend,
            assignment_id,
            state.plane_name,
            node_name,
            index,
            artifacts.size(),
            aggregate_prefix,
            aggregate_total);
        artifact_size = transfer_support_.FileSizeIfExists(artifact.target_host_path);
      }
    } else if (artifact.source_url.has_value() && !artifact.source_url->empty()) {
      if (!IsArtifactAlreadyPresent(artifact)) {
        transfer_support_.DownloadFileWithProgress(
            *artifact.source_url,
            artifact.target_host_path,
            backend,
            assignment_id,
            state.plane_name,
            node_name,
            index,
            artifacts.size(),
            aggregate_prefix,
            aggregate_total);
        artifact_size = transfer_support_.FileSizeIfExists(artifact.target_host_path);
      }
    }
    if (artifact_size.has_value()) {
      aggregate_prefix += *artifact_size;
    }
  }
}

void HostdBootstrapModelSupport::VerifyBootstrapChecksumIfNeeded(
    const comet::DesiredState& state,
    const std::string& node_name,
    const comet::BootstrapModelSpec& bootstrap_model,
    const std::vector<HostdBootstrapModelArtifact>& artifacts,
    const std::string& target_path,
    bool already_present,
    HostdBackend* backend,
    const std::optional<int>& assignment_id) const {
  if (!bootstrap_model.sha256.has_value()) {
    return;
  }
  if (artifacts.size() > 1) {
    throw std::runtime_error(
        "bootstrap_model.sha256 is not supported with multipart bootstrap_model.source_urls");
  }
  if (fs::is_directory(target_path)) {
    throw std::runtime_error(
        "bootstrap_model.sha256 is not supported for directory-based bootstrap models");
  }

  PublishAssignmentProgress(
      backend,
      assignment_id,
      "verifying-model",
      "Verifying model",
      already_present ? "Checking the existing shared-disk model checksum."
                      : "Verifying the model checksum in the shared disk.",
      already_present ? 68 : 72,
      state.plane_name,
      node_name);
  if (!transfer_support_.CheckFileSha256Hex(target_path, *bootstrap_model.sha256)) {
    throw std::runtime_error("bootstrap model checksum mismatch for " + target_path);
  }
}

bool HostdBootstrapModelSupport::HasBootstrapSource(
    const comet::BootstrapModelSpec& bootstrap_model) {
  return (bootstrap_model.local_path.has_value() && !bootstrap_model.local_path->empty()) ||
         (bootstrap_model.source_url.has_value() && !bootstrap_model.source_url->empty()) ||
         !bootstrap_model.source_urls.empty();
}

void HostdBootstrapModelSupport::BootstrapPlaneModelIfNeeded(
    const comet::DesiredState& state,
    const std::string& node_name,
    HostdBackend* backend,
    const std::optional<int>& assignment_id) const {
  if (state.instances.empty()) {
    return;
  }

  const auto& shared_disk = artifact_support_.RequirePlaneSharedDiskForNode(state, node_name);
  if (!fs::exists(shared_disk.host_path)) {
    throw std::runtime_error(
        "plane shared disk path does not exist after ensure-disk: " + shared_disk.host_path);
  }

  PublishAssignmentProgress(
      backend,
      assignment_id,
      "ensuring-shared-disk",
      "Ensuring shared disk",
      "Plane shared disk is mounted and ready for model/bootstrap data.",
      12,
      state.plane_name,
      node_name);

  const std::string active_model_path =
      active_model_support_.ActiveModelPathForNode(state, node_name);
  if (!state.bootstrap_model.has_value()) {
    file_support_.RemoveFileIfExists(active_model_path);
    return;
  }

  const auto& bootstrap_model = *state.bootstrap_model;
  if (TryUseReferenceBootstrapModel(
          state,
          node_name,
          bootstrap_model,
          backend,
          assignment_id)) {
    return;
  }

  const std::string target_path = artifact_support_.TargetPath(state, node_name);
  const auto artifacts = artifact_support_.BuildArtifacts(state, node_name);
  if (TryUseSharedBootstrapFromOtherNode(
          state,
          node_name,
          artifacts,
          target_path,
          backend,
          assignment_id)) {
    return;
  }

  bool already_present = false;
  const auto aggregate_total = ComputeAggregateExpectedSize(artifacts, already_present);
  if (already_present) {
    VerifyBootstrapChecksumIfNeeded(
        state,
        node_name,
        bootstrap_model,
        artifacts,
        target_path,
        true,
        backend,
        assignment_id);
  }

  AcquireArtifactsIfNeeded(
      state,
      node_name,
      artifacts,
      aggregate_total,
      already_present,
      backend,
      assignment_id);
  VerifyBootstrapChecksumIfNeeded(
      state,
      node_name,
      bootstrap_model,
      artifacts,
      target_path,
      false,
      backend,
      assignment_id);

  if (!HasBootstrapSource(bootstrap_model) && !fs::exists(target_path)) {
    file_support_.RemoveFileIfExists(active_model_path);
    return;
  }

  PublishAssignmentProgress(
      backend,
      assignment_id,
      "activating-model",
      "Activating model",
      "Writing active-model.json for infer and worker runtime.",
      80,
      state.plane_name,
      node_name);
  active_model_support_.WriteBootstrapActiveModel(state, node_name, target_path);
}

}  // namespace comet::hostd
