#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "naim/state/sqlite_store.h"

namespace naim::hostd {

class HostdBackend {
 public:
  virtual ~HostdBackend() = default;

  virtual std::optional<naim::HostAssignment> ClaimNextHostAssignment(
      const std::string& node_name) = 0;
  virtual bool TransitionClaimedHostAssignment(
      int assignment_id,
      naim::HostAssignmentStatus status,
      const std::string& status_message) = 0;
  virtual bool UpdateHostAssignmentProgress(
      int assignment_id,
      const nlohmann::json& progress) = 0;
  virtual void UpsertHostObservation(const naim::HostObservation& observation) = 0;
  virtual void AppendEvent(const naim::EventRecord& event) = 0;
  virtual void UpsertDiskRuntimeState(const naim::DiskRuntimeState& state) = 0;
  virtual std::optional<naim::DiskRuntimeState> LoadDiskRuntimeState(
      const std::string& disk_name,
      const std::string& node_name) = 0;
};

}  // namespace naim::hostd
