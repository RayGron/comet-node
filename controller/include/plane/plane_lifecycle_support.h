#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "plane/desired_state_policy_service.h"
#include "plane/plane_realization_service.h"

namespace comet::controller {

class PlaneLifecycleSupport {
 public:
  virtual ~PlaneLifecycleSupport() = default;

  virtual void PrepareDesiredState(
      comet::ControllerStore& store,
      comet::DesiredState* desired_state) const = 0;
  virtual void AppendPlaneEvent(
      comet::ControllerStore& store,
      const std::string& event_type,
      const std::string& message,
      const nlohmann::json& payload,
      const std::string& plane_name) const = 0;
  virtual bool CanFinalizeDeletedPlane(
      comet::ControllerStore& store,
      const std::string& plane_name) const = 0;
  virtual std::optional<comet::HostAssignment> FindLatestHostAssignmentForPlane(
      const std::vector<comet::HostAssignment>& assignments,
      const std::string& plane_name) const = 0;
  virtual std::vector<comet::HostAssignment> BuildStartAssignments(
      const comet::DesiredState& desired_state,
      const std::string& artifacts_root,
      int desired_generation,
      const std::vector<comet::NodeAvailabilityOverride>& availability_overrides,
      const std::vector<comet::HostObservation>& observations,
      const comet::SchedulingPolicyReport& scheduling_report) const = 0;
  virtual std::vector<comet::HostAssignment> BuildStopAssignments(
      const comet::DesiredState& desired_state,
      int desired_generation,
      const std::string& artifacts_root,
      const std::vector<comet::NodeAvailabilityOverride>& availability_overrides) const = 0;
  virtual std::vector<comet::HostAssignment> BuildDeleteAssignments(
      const comet::DesiredState& desired_state,
      int desired_generation,
      const std::string& artifacts_root) const = 0;
  virtual std::string DefaultArtifactsRoot() const = 0;
};

class ControllerPlaneLifecycleSupport final : public PlaneLifecycleSupport {
 public:
  ControllerPlaneLifecycleSupport(
      const DesiredStatePolicyService& desired_state_policy_service,
      const PlaneRealizationService& plane_realization_service,
      std::string default_artifacts_root);

  void PrepareDesiredState(
      comet::ControllerStore& store,
      comet::DesiredState* desired_state) const override;
  void AppendPlaneEvent(
      comet::ControllerStore& store,
      const std::string& event_type,
      const std::string& message,
      const nlohmann::json& payload,
      const std::string& plane_name) const override;
  bool CanFinalizeDeletedPlane(
      comet::ControllerStore& store,
      const std::string& plane_name) const override;
  std::optional<comet::HostAssignment> FindLatestHostAssignmentForPlane(
      const std::vector<comet::HostAssignment>& assignments,
      const std::string& plane_name) const override;
  std::vector<comet::HostAssignment> BuildStartAssignments(
      const comet::DesiredState& desired_state,
      const std::string& artifacts_root,
      int desired_generation,
      const std::vector<comet::NodeAvailabilityOverride>& availability_overrides,
      const std::vector<comet::HostObservation>& observations,
      const comet::SchedulingPolicyReport& scheduling_report) const override;
  std::vector<comet::HostAssignment> BuildStopAssignments(
      const comet::DesiredState& desired_state,
      int desired_generation,
      const std::string& artifacts_root,
      const std::vector<comet::NodeAvailabilityOverride>& availability_overrides) const override;
  std::vector<comet::HostAssignment> BuildDeleteAssignments(
      const comet::DesiredState& desired_state,
      int desired_generation,
      const std::string& artifacts_root) const override;
  std::string DefaultArtifactsRoot() const override;

 private:
  const DesiredStatePolicyService& desired_state_policy_service_;
  const PlaneRealizationService& plane_realization_service_;
  std::string default_artifacts_root_;
};

}  // namespace comet::controller
