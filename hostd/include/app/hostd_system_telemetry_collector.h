#pragma once

#include <string>
#include <vector>

#include "app/hostd_command_support.h"
#include "naim/runtime/runtime_status.h"
#include "naim/state/models.h"

namespace naim::hostd {

class HostdSystemTelemetryCollector final {
public:
  naim::GpuTelemetrySnapshot CollectGpuTelemetry(
    const naim::DesiredState& state,
    const std::string& node_name,
    const std::vector<naim::RuntimeProcessStatus>& instance_statuses) const;

  naim::DiskTelemetrySnapshot CollectDiskTelemetry(
    const naim::DesiredState& state,
    const std::string& node_name) const;

  naim::DiskTelemetryRecord BuildStorageRootTelemetry(
    const std::string& node_name,
    const std::string& storage_root) const;

  naim::CpuTelemetrySnapshot CollectCpuTelemetry() const;
  naim::NetworkTelemetrySnapshot CollectNetworkTelemetry(
    const std::string& state_root) const;

private:

  std::vector<std::string> splitCsvRow(
    const std::string& line) const;

#ifdef NAIM_RUNTIME_CUDA
  void populateGpuProcessesFromNvidiaSMI(
    naim::GpuTelemetrySnapshot* snapshot,
    const std::vector<RuntimeProcessStatus> &instance_statuses) const;

  std::optional<naim::GpuTelemetrySnapshot> collectGpuTelemetryWithNVML(
    const naim::DesiredState& state,
    const std::string& node_name) const;

  std::optional<naim::GpuTelemetrySnapshot> collectGpuTelemetryWithNvidiaSMI(
    const naim::DesiredState& state,
    const std::string& node_name,
    const std::vector<naim::RuntimeProcessStatus>& instance_statuses) const;
#endif

#ifdef NAIM_RUNTIME_VULKAN
  std::optional<naim::GpuTelemetrySnapshot> collectGpuTelemetryWithVulkanAPI() const;
#endif

  HostdCommandSupport command_support_;
};

}  // namespace naim::hostd
