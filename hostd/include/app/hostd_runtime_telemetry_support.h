#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "app/hostd_command_support.h"
#include "app/hostd_desired_state_path_support.h"
#include "comet/runtime/runtime_status.h"
#include "comet/state/models.h"

namespace comet::hostd {

class HostdRuntimeTelemetrySupport final {
 public:
  std::vector<comet::RuntimeProcessStatus> LoadLocalInstanceRuntimeStatuses(
      const std::string& state_root,
      const std::string& node_name,
      const std::optional<std::string>& plane_name = std::nullopt) const;

  std::vector<std::string> ParseTaggedCsv(
      const std::string& tagged_message,
      const std::string& tag) const;

  std::map<std::string, int> CaptureServiceHostPids(
      const std::vector<std::string>& service_names) const;

  bool VerifyEvictionAssignment(
      const comet::DesiredState& desired_state,
      const std::string& node_name,
      const std::string& state_root,
      const std::string& tagged_message,
      const std::map<std::string, int>& expected_victim_host_pids) const;

  void ResolveInstanceHostPids(std::vector<comet::RuntimeProcessStatus>* statuses) const;

 private:
  std::optional<std::string> WorkerRuntimeStatusPathForInstance(
      const comet::DesiredState& state,
      const comet::InstanceSpec& instance) const;
  comet::RuntimeProcessStatus ToProcessStatus(
      comet::RuntimeStatus status,
      const comet::InstanceSpec& instance) const;
  std::optional<std::string> ParseTaggedValue(
      const std::string& text,
      const std::string& key) const;
  std::vector<std::string> SplitCsvRow(const std::string& line) const;
  std::optional<std::string> ResolveComposeContainerIdForService(
      const std::string& service_name) const;
  std::optional<int> ResolveServiceHostPid(const std::string& service_name) const;
  bool IsContainerAbsentForService(const std::string& service_name) const;

  HostdCommandSupport command_support_;
  HostdDesiredStatePathSupport path_support_;
};

}  // namespace comet::hostd
