#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "comet/state/sqlite_store.h"

namespace comet::hostd {

class HostdBackend {
 public:
  virtual ~HostdBackend() = default;

  virtual std::optional<comet::HostAssignment> ClaimNextHostAssignment(
      const std::string& node_name) = 0;
  virtual bool TransitionClaimedHostAssignment(
      int assignment_id,
      comet::HostAssignmentStatus status,
      const std::string& status_message) = 0;
  virtual bool UpdateHostAssignmentProgress(
      int assignment_id,
      const nlohmann::json& progress) = 0;
  virtual void UpsertHostObservation(const comet::HostObservation& observation) = 0;
  virtual void AppendEvent(const comet::EventRecord& event) = 0;
  virtual void UpsertDiskRuntimeState(const comet::DiskRuntimeState& state) = 0;
  virtual std::optional<comet::DiskRuntimeState> LoadDiskRuntimeState(
      const std::string& disk_name,
      const std::string& node_name) = 0;
};

}  // namespace comet::hostd
