#include "comet/scheduling_policy.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace comet {

namespace {

constexpr double kFractionEpsilon = 1e-9;

std::string JoinStrings(const std::vector<std::string>& values, const std::string& delimiter) {
  std::ostringstream out;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0) {
      out << delimiter;
    }
    out << values[index];
  }
  return out.str();
}

struct AllocationAccumulator {
  std::vector<std::string> worker_names;
  double total_fraction = 0.0;
};

}  // namespace

SchedulingPolicyReport EvaluateSchedulingPolicy(const DesiredState& state) {
  SchedulingPolicyReport report;

  std::map<std::string, NodeInventory> nodes_by_name;
  for (const auto& node : state.nodes) {
    nodes_by_name.emplace(node.name, node);
  }

  std::map<std::pair<std::string, std::string>, AllocationAccumulator> allocations;

  for (const auto& instance : state.instances) {
    if (instance.role != InstanceRole::Worker) {
      continue;
    }

    if (!instance.gpu_device.has_value() || instance.gpu_device->empty()) {
      report.errors.push_back(
          "worker '" + instance.name +
          "' must pin gpu_device explicitly; implicit scheduler defaults are not allowed");
      continue;
    }

    if (instance.gpu_fraction <= 0.0 || instance.gpu_fraction > 1.0 + kFractionEpsilon) {
      std::ostringstream message;
      message << "worker '" << instance.name << "' has invalid gpu_fraction=" << instance.gpu_fraction
              << "; allowed range is (0, 1]";
      report.errors.push_back(message.str());
      continue;
    }

    const auto node_it = nodes_by_name.find(instance.node_name);
    if (node_it == nodes_by_name.end()) {
      report.errors.push_back(
          "worker '" + instance.name + "' references unknown node '" + instance.node_name + "'");
      continue;
    }

    const auto& node = node_it->second;
    const auto gpu_it = std::find(
        node.gpu_devices.begin(), node.gpu_devices.end(), *instance.gpu_device);
    if (gpu_it == node.gpu_devices.end()) {
      report.errors.push_back(
          "worker '" + instance.name + "' pins missing gpu '" + *instance.gpu_device +
          "' on node '" + instance.node_name + "'");
      continue;
    }

    auto& allocation = allocations[{instance.node_name, *instance.gpu_device}];
    allocation.worker_names.push_back(instance.name);
    allocation.total_fraction += instance.gpu_fraction;
  }

  for (auto& [key, allocation] : allocations) {
    std::sort(allocation.worker_names.begin(), allocation.worker_names.end());

    GpuAllocationGroup group;
    group.node_name = key.first;
    group.gpu_device = key.second;
    group.worker_names = allocation.worker_names;
    group.total_fraction = allocation.total_fraction;
    group.remaining_fraction = std::max(0.0, 1.0 - allocation.total_fraction);
    report.allocations.push_back(group);

    if (allocation.total_fraction > 1.0 + kFractionEpsilon) {
      std::ostringstream message;
      message << "gpu oversubscription on node '" << key.first << "' gpu '" << key.second
              << "': total requested fraction=" << allocation.total_fraction
              << " by workers " << JoinStrings(allocation.worker_names, ",");
      report.errors.push_back(message.str());
    } else if (allocation.worker_names.size() > 1) {
      std::ostringstream message;
      message << "soft-share group on node '" << key.first << "' gpu '" << key.second
              << "': workers=" << JoinStrings(allocation.worker_names, ",")
              << " total_fraction=" << allocation.total_fraction;
      report.warnings.push_back(message.str());
    }
  }

  std::sort(
      report.allocations.begin(),
      report.allocations.end(),
      [](const GpuAllocationGroup& left, const GpuAllocationGroup& right) {
        if (left.node_name != right.node_name) {
          return left.node_name < right.node_name;
        }
        return left.gpu_device < right.gpu_device;
      });

  return report;
}

std::string RenderSchedulingPolicyReport(const SchedulingPolicyReport& report) {
  std::ostringstream out;
  out << "scheduling-policy:\n";
  if (report.allocations.empty()) {
    out << "  allocations=(empty)\n";
  } else {
    for (const auto& allocation : report.allocations) {
      out << "  - node=" << allocation.node_name
          << " gpu=" << allocation.gpu_device
          << " workers=" << JoinStrings(allocation.worker_names, ",")
          << " total_fraction=" << allocation.total_fraction
          << " remaining_fraction=" << allocation.remaining_fraction << "\n";
    }
  }

  if (!report.warnings.empty()) {
    out << "  warnings:\n";
    for (const auto& warning : report.warnings) {
      out << "    - " << warning << "\n";
    }
  }

  if (!report.errors.empty()) {
    out << "  errors:\n";
    for (const auto& error : report.errors) {
      out << "    - " << error << "\n";
    }
  }

  return out.str();
}

void RequireSchedulingPolicy(const DesiredState& state) {
  const SchedulingPolicyReport report = EvaluateSchedulingPolicy(state);
  if (report.errors.empty()) {
    return;
  }

  std::ostringstream message;
  message << "desired state violates scheduling policy:";
  for (const auto& error : report.errors) {
    message << "\n- " << error;
  }
  throw std::runtime_error(message.str());
}

}  // namespace comet
