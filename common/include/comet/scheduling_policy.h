#pragma once

#include <string>
#include <vector>

#include "comet/models.h"

namespace comet {

struct GpuAllocationGroup {
  std::string node_name;
  std::string gpu_device;
  std::vector<std::string> worker_names;
  double total_fraction = 0.0;
  double remaining_fraction = 1.0;
};

struct SchedulingPolicyReport {
  std::vector<GpuAllocationGroup> allocations;
  std::vector<std::string> warnings;
  std::vector<std::string> errors;
};

SchedulingPolicyReport EvaluateSchedulingPolicy(const DesiredState& state);
std::string RenderSchedulingPolicyReport(const SchedulingPolicyReport& report);
void RequireSchedulingPolicy(const DesiredState& state);

}  // namespace comet
