#pragma once

#include <optional>
#include <string>
#include <vector>

#include "app/hostd_desired_state_path_support.h"
#include "comet/state/models.h"

namespace comet::hostd {

struct HostdBootstrapModelArtifact {
  std::optional<std::string> local_path;
  std::optional<std::string> source_url;
  std::string target_host_path;
};

class HostdBootstrapModelArtifactSupport final {
 public:
  explicit HostdBootstrapModelArtifactSupport(
      const HostdDesiredStatePathSupport& path_support);

  const comet::DiskSpec& RequirePlaneSharedDiskForNode(
      const comet::DesiredState& state,
      const std::string& node_name) const;
  std::vector<HostdBootstrapModelArtifact> BuildArtifacts(
      const comet::DesiredState& state,
      const std::string& node_name) const;
  std::string TargetPath(
      const comet::DesiredState& state,
      const std::string& node_name) const;
  std::string SharedModelBootstrapOwnerNode(const comet::DesiredState& state) const;
  static bool LooksLikeRecognizedModelDirectory(const std::string& path);

 private:
  static std::string FilenameFromUrl(const std::string& source_url);

  const HostdDesiredStatePathSupport& path_support_;
};

}  // namespace comet::hostd
