#pragma once

#include <optional>
#include <string>

#include "app/hostd_bootstrap_model_artifact_support.h"
#include "app/hostd_desired_state_path_support.h"
#include "app/hostd_file_support.h"
#include "app/hostd_local_state_path_support.h"
#include "app/hostd_local_state_repository.h"
#include "comet/state/models.h"

namespace comet::hostd {

class HostdBootstrapActiveModelSupport final {
 public:
  HostdBootstrapActiveModelSupport(
      const HostdDesiredStatePathSupport& path_support,
      const HostdFileSupport& file_support,
      const HostdBootstrapModelArtifactSupport& artifact_support);

  std::string ActiveModelPathForNode(
      const comet::DesiredState& state,
      const std::string& node_name) const;
  std::string BootstrapRuntimeModelPath(
      const comet::DesiredState& state,
      const std::string& target_host_path) const;
  void WriteBootstrapActiveModel(
      const comet::DesiredState& state,
      const std::string& node_name,
      const std::string& target_host_path,
      const std::optional<std::string>& runtime_model_path_override = std::nullopt) const;

 private:
  const HostdDesiredStatePathSupport& path_support_;
  const HostdFileSupport& file_support_;
  const HostdBootstrapModelArtifactSupport& artifact_support_;
  HostdLocalStatePathSupport local_state_path_support_;
  HostdLocalStateRepository local_state_repository_;
};

}  // namespace comet::hostd
