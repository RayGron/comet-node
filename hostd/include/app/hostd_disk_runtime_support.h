#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include "app/hostd_command_support.h"
#include "app/hostd_desired_state_path_support.h"
#include "app/hostd_file_support.h"
#include "backend/hostd_backend.h"
#include "comet/planning/execution_plan.h"
#include "comet/state/models.h"

namespace comet::hostd {

class HostdDiskRuntimeSupport final {
 public:
  HostdDiskRuntimeSupport(
      const HostdCommandSupport& command_support,
      const HostdDesiredStatePathSupport& path_support,
      const HostdFileSupport& file_support);

  std::optional<comet::DiskSpec> FindDiskInStateByKey(
      const std::optional<comet::DesiredState>& state,
      const std::string& disk_key) const;
  std::pair<std::string, std::string> SplitDiskKey(const std::string& disk_key) const;

  comet::DiskRuntimeState EnsureDesiredDiskRuntimeState(
      const comet::DiskSpec& disk,
      const std::string& disk_key,
      const std::string& storage_root,
      const std::optional<std::string>& runtime_root) const;

  void PersistDiskRuntimeStateForDesiredDisks(
      HostdBackend* backend,
      const comet::DesiredState& desired_node_state,
      const std::string& storage_root,
      const std::optional<std::string>& runtime_root,
      const std::string& status_message) const;

  void EnsureDesiredDisksReady(
      HostdBackend* backend,
      const comet::DesiredState& desired_node_state,
      const std::string& storage_root,
      const std::optional<std::string>& runtime_root) const;

  void PersistDiskRuntimeStateForRemovedDisks(
      HostdBackend* backend,
      const std::optional<comet::DesiredState>& previous_state,
      const comet::NodeExecutionPlan& execution_plan) const;

 void RemoveRealDiskMount(
      const comet::DiskRuntimeState& runtime_state,
      const std::optional<std::string>& runtime_root) const;

 private:
  bool IsUnderRoot(
      const std::filesystem::path& path,
      const std::optional<std::string>& runtime_root) const;
  std::string SanitizeDiskPathComponent(const std::string& value) const;
  std::string ManagedDiskImagePath(
      const comet::DiskSpec& disk,
      const std::string& storage_root,
      const std::optional<std::string>& runtime_root) const;
  void EnsureDiskDirectory(const std::string& path, const std::string& disk_key) const;
  void RemoveDiskDirectory(
      const std::string& path,
      const std::optional<std::string>& runtime_root) const;
  bool HostCanManageRealDisks() const;
  std::string NormalizeManagedPath(const std::string& path) const;
  std::string NormalizeLoopImagePath(const std::string& image_path) const;
  std::string NormalizeMountPointPath(const std::string& mount_point) const;
  std::optional<std::string> DetectExistingLoopDevice(const std::string& image_path) const;
  std::string RequireLoopDeviceForImage(const std::string& image_path) const;
  std::string DetectFilesystemTypeForDevice(const std::string& device_path) const;
  bool IsPathMounted(const std::string& mount_point) const;
  std::optional<std::string> CurrentMountSource(const std::string& mount_point) const;
  void CreateSparseImageFile(const std::string& image_path, int size_gb) const;
  bool IsSharedManagedDiskImagePath(const std::string& image_path) const;
  comet::DiskRuntimeState BuildDiskRuntimeState(
      const comet::DiskSpec& disk,
      const std::string& runtime_state,
      const std::string& status_message) const;
  comet::DiskRuntimeState EnsureRealDiskMount(
      const comet::DiskSpec& disk,
      const std::string& storage_root,
      const std::optional<std::string>& runtime_root) const;
  comet::DiskRuntimeState InspectRealDiskRuntime(
      const comet::DiskSpec& disk,
      const std::string& storage_root,
      const std::optional<std::string>& runtime_root) const;

  const HostdCommandSupport& command_support_;
  const HostdDesiredStatePathSupport& path_support_;
  const HostdFileSupport& file_support_;
};

}  // namespace comet::hostd
