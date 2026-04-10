#pragma once

#include <string>
#include <vector>

#include "app/hostd_command_support.h"
#include "comet/runtime/runtime_status.h"
#include "comet/state/models.h"

namespace comet::hostd {

class HostdSystemTelemetryCollector final {
 public:
  comet::GpuTelemetrySnapshot CollectGpuTelemetry(
      const comet::DesiredState& state,
      const std::string& node_name,
      const std::vector<comet::RuntimeProcessStatus>& instance_statuses) const;

  comet::DiskTelemetrySnapshot CollectDiskTelemetry(
      const comet::DesiredState& state,
      const std::string& node_name) const;

  comet::DiskTelemetryRecord BuildStorageRootTelemetry(
      const std::string& node_name,
      const std::string& storage_root) const;

  comet::CpuTelemetrySnapshot CollectCpuTelemetry() const;
  comet::NetworkTelemetrySnapshot CollectNetworkTelemetry() const;

 private:
  HostdCommandSupport command_support_;
};

}  // namespace comet::hostd
