#include "scheduler/scheduler_domain_support.h"

namespace comet::controller {

ControllerSchedulerDomainSupport::ControllerSchedulerDomainSupport(
    const ControllerRuntimeSupportService& runtime_support_service,
    const PlaneRealizationService& plane_realization_service)
    : runtime_support_service_(runtime_support_service),
      plane_realization_service_(plane_realization_service) {}

std::optional<long long> ControllerSchedulerDomainSupport::HeartbeatAgeSeconds(
    const std::string& heartbeat_at) const {
  return runtime_support_service_.HeartbeatAgeSeconds(heartbeat_at);
}

std::string ControllerSchedulerDomainSupport::HealthFromAge(
    const std::optional<long long>& age_seconds,
    int stale_after_seconds) const {
  return runtime_support_service_.HealthFromAge(age_seconds, stale_after_seconds);
}

std::optional<comet::RuntimeStatus> ControllerSchedulerDomainSupport::ParseRuntimeStatus(
    const comet::HostObservation& observation) const {
  return runtime_support_service_.ParseRuntimeStatus(observation);
}

std::optional<comet::GpuTelemetrySnapshot> ControllerSchedulerDomainSupport::ParseGpuTelemetry(
    const comet::HostObservation& observation) const {
  return runtime_support_service_.ParseGpuTelemetry(observation);
}

std::map<std::string, comet::NodeAvailabilityOverride>
ControllerSchedulerDomainSupport::BuildAvailabilityOverrideMap(
    const std::vector<comet::NodeAvailabilityOverride>& availability_overrides) const {
  return runtime_support_service_.BuildAvailabilityOverrideMap(availability_overrides);
}

comet::NodeAvailability ControllerSchedulerDomainSupport::ResolveNodeAvailability(
    const std::map<std::string, comet::NodeAvailabilityOverride>& availability_overrides,
    const std::string& node_name) const {
  return runtime_support_service_.ResolveNodeAvailability(availability_overrides, node_name);
}

bool ControllerSchedulerDomainSupport::IsNodeSchedulable(
    comet::NodeAvailability availability) const {
  return availability == comet::NodeAvailability::Active;
}

std::optional<long long> ControllerSchedulerDomainSupport::TimestampAgeSeconds(
    const std::string& timestamp_text) const {
  return runtime_support_service_.TimestampAgeSeconds(timestamp_text);
}

std::optional<std::string> ControllerSchedulerDomainSupport::ObservedSchedulingGateReason(
    const std::vector<comet::HostObservation>& observations,
    const std::string& node_name,
    int stale_after_seconds) const {
  return plane_realization_service_.ObservedSchedulingGateReason(
      observations,
      node_name,
      stale_after_seconds);
}

}  // namespace comet::controller
