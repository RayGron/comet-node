#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "comet/state/models.h"

namespace comet::hostd {

class HostdDesiredStatePathSupport final {
 public:
  static constexpr const char* kDefaultManagedStorageRoot = "/var/lib/comet";

  std::string RebaseManagedStorageRoot(
      const std::string& path,
      const std::string& storage_root) const;
  std::string RebaseManagedPath(
      const std::string& path,
      const std::string& storage_root,
      const std::optional<std::string>& runtime_root,
      const std::optional<std::string>& node_name = std::nullopt) const;
  comet::DesiredState RebaseStateForRuntimeRoot(
      comet::DesiredState state,
      const std::string& storage_root,
      const std::optional<std::string>& runtime_root) const;
  const comet::DiskSpec* FindSharedDiskForNode(
      const comet::DesiredState& state,
      const std::string& node_name) const;
  std::optional<std::string> ControlFilePathForNode(
      const comet::DesiredState& state,
      const std::string& node_name,
      const std::string& file_name) const;
  std::optional<std::string> InferRuntimeConfigPathForNode(
      const comet::DesiredState& state,
      const std::string& node_name) const;
  std::optional<std::string> InferRuntimeStatusPathForInstance(
      const comet::DesiredState& state,
      const comet::InstanceSpec& infer_instance) const;
  std::optional<std::string> RuntimeStatusPathForNode(
      const comet::DesiredState& state,
      const std::string& node_name) const;
  std::string SharedDiskHostPathForContainerPath(
      const comet::DiskSpec& shared_disk,
      const std::string& container_path,
      const std::string& fallback_relative_path) const;

 private:
  bool HasPathPrefix(
      const std::filesystem::path& path,
      const std::filesystem::path& prefix) const;
  const comet::InstanceSpec* PrimaryInferInstanceForNode(
      const comet::DesiredState& state,
      const std::string& node_name) const;
};

}  // namespace comet::hostd
