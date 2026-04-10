#pragma once

#include <optional>
#include <string>
#include <vector>

#include "infra/controller_runtime_support_service.h"
#include "plane/plane_realization_service.h"

namespace comet::controller {

class SchedulerAssignmentQuerySupport {
 public:
  virtual ~SchedulerAssignmentQuerySupport() = default;

  virtual std::optional<comet::HostAssignment> FindLatestHostAssignmentForNode(
      const std::vector<comet::HostAssignment>& assignments,
      const std::string& node_name) const = 0;
  virtual std::optional<comet::HostAssignment> FindLatestHostAssignmentForPlane(
      const std::vector<comet::HostAssignment>& assignments,
      const std::string& plane_name) const = 0;
  virtual std::string DefaultArtifactsRoot() const = 0;
};

class SchedulerVerificationSupport {
 public:
  virtual ~SchedulerVerificationSupport() = default;

  virtual std::optional<comet::HostObservation> FindHostObservationForNode(
      const std::vector<comet::HostObservation>& observations,
      const std::string& node_name) const = 0;
  virtual std::vector<comet::RuntimeProcessStatus> ParseInstanceRuntimeStatuses(
      const comet::HostObservation& observation) const = 0;
  virtual std::optional<comet::GpuTelemetrySnapshot> ParseGpuTelemetry(
      const comet::HostObservation& observation) const = 0;
  virtual std::optional<long long> TimestampAgeSeconds(
      const std::string& timestamp_text) const = 0;
  virtual std::string UtcNowSqlTimestamp() const = 0;
};

class ControllerSchedulerAssignmentQuerySupport final
    : public SchedulerAssignmentQuerySupport {
 public:
  ControllerSchedulerAssignmentQuerySupport(
      const PlaneRealizationService& plane_realization_service,
      std::string default_artifacts_root);

  std::optional<comet::HostAssignment> FindLatestHostAssignmentForNode(
      const std::vector<comet::HostAssignment>& assignments,
      const std::string& node_name) const override;
  std::optional<comet::HostAssignment> FindLatestHostAssignmentForPlane(
      const std::vector<comet::HostAssignment>& assignments,
      const std::string& plane_name) const override;
  std::string DefaultArtifactsRoot() const override;

 private:
  const PlaneRealizationService& plane_realization_service_;
  std::string default_artifacts_root_;
};

class ControllerSchedulerVerificationSupport final
    : public SchedulerVerificationSupport {
 public:
  explicit ControllerSchedulerVerificationSupport(
      const ControllerRuntimeSupportService& runtime_support_service);

  std::optional<comet::HostObservation> FindHostObservationForNode(
      const std::vector<comet::HostObservation>& observations,
      const std::string& node_name) const override;
  std::vector<comet::RuntimeProcessStatus> ParseInstanceRuntimeStatuses(
      const comet::HostObservation& observation) const override;
  std::optional<comet::GpuTelemetrySnapshot> ParseGpuTelemetry(
      const comet::HostObservation& observation) const override;
  std::optional<long long> TimestampAgeSeconds(
      const std::string& timestamp_text) const override;
  std::string UtcNowSqlTimestamp() const override;

 private:
  const ControllerRuntimeSupportService& runtime_support_service_;
};

}  // namespace comet::controller
