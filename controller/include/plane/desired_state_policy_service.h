#pragma once

#include <map>
#include <optional>
#include <string>

#include "infra/controller_runtime_support_service.h"

#include "comet/state/models.h"
#include "comet/state/sqlite_store.h"

namespace comet::controller {

class DesiredStatePolicyService {
 public:
  std::optional<std::string> DescribeUnsupportedControllerLocalRuntime(
      const comet::DesiredState& desired_state,
      const std::string& node_name) const;

  void ApplyRegisteredHostExecutionModes(
      comet::ControllerStore& store,
      comet::DesiredState* desired_state) const;

  void ResolveDesiredStateDynamicPlacements(
      comet::ControllerStore& store,
      comet::DesiredState* desired_state) const;

  void ValidateDesiredStateForControllerAdmission(
      comet::ControllerStore& store,
      const comet::DesiredState& desired_state) const;

  void ValidateDesiredStateExecutionModes(
      const comet::DesiredState& desired_state) const;

 private:
  struct PlacementUsage {
    double allocated_fraction = 0.0;
    int allocated_memory_mb = 0;
  };

  struct AutoPlacementDecision {
    std::string node_name;
    std::string gpu_device;
    int score = 0;
    bool idle_target = false;
    bool upgrade_to_exclusive = false;
    double allocated_fraction = 0.0;
    int allocated_memory_mb = 0;
    int observed_free_vram_mb = -1;
    int observed_utilization_pct = -1;
    int node_order = 0;
    int gpu_order = 0;
  };

  std::string CurrentControllerPlatform() const;
  const comet::NodeInventory* FindPlaneNodeInventory(
      const comet::DesiredState& desired_state,
      const std::string& node_name) const;
  bool PlaneNodeUsesGpuRuntime(
      const comet::DesiredState& desired_state,
      const std::string& node_name) const;
  bool NodeAllowsInstanceRole(
      comet::HostExecutionMode execution_mode,
      comet::InstanceRole role) const;
  std::string EffectiveWorkerSelectionPolicy(
      const comet::DesiredState& state) const;
  int AutoPlacementPolicyRank(
      const std::string& policy,
      const AutoPlacementDecision& candidate) const;
  int ScoreAutoPlacementCandidate(
      const comet::NodeInventory& node,
      const std::string& gpu_device,
      const PlacementUsage& usage,
      const comet::InferenceRuntimeSettings& inference,
      int observed_free_vram_mb,
      int observed_utilization_pct,
      const std::optional<std::string>& preferred_node_name,
      const std::optional<std::string>& preferred_gpu_device) const;
  bool HybridGpuAlreadyAssigned(
      const comet::DesiredState& desired_state,
      const comet::InstanceSpec& current_worker,
      const std::string& node_name,
      const std::string& gpu_device) const;
  bool UsesLlamaRpcRuntime(const comet::DesiredState& desired_state) const;
  void ReservePlacement(
      std::map<std::pair<std::string, std::string>, PlacementUsage>* placement_usage,
      const comet::InstanceSpec& worker) const;
  const comet::InstanceSpec* FindInferInstance(
      const comet::DesiredState& desired_state) const;
  std::string InferInstanceNameForWorker(const comet::InstanceSpec& instance) const;
  void RefreshDerivedWorkerMetadata(
      comet::DesiredState* desired_state) const;
  void ApplyObservedHostGpuInventory(
      comet::ControllerStore& store,
      comet::DesiredState* desired_state) const;
  std::optional<AutoPlacementDecision> SelectAutoPlacement(
      const comet::DesiredState& desired_state,
      const std::map<std::pair<std::string, std::string>, PlacementUsage>& placement_usage,
      const std::map<std::pair<std::string, std::string>, std::pair<int, int>>&
          observed_gpu_headroom,
      const comet::InstanceSpec& worker,
      const std::optional<std::string>& requested_node_name,
      const std::optional<std::string>& requested_gpu_device) const;

  ControllerRuntimeSupportService runtime_support_;
};

}  // namespace comet::controller
