#pragma once

#include <string>

#include "backend/hostd_backend.h"

namespace naim::hostd {

class LocalDbHostdBackend final : public HostdBackend {
 public:
  explicit LocalDbHostdBackend(std::string db_path);

  std::optional<naim::HostAssignment> ClaimNextHostAssignment(
      const std::string& node_name) override;
  bool TransitionClaimedHostAssignment(
      int assignment_id,
      naim::HostAssignmentStatus status,
      const std::string& status_message) override;
  bool UpdateHostAssignmentProgress(
      int assignment_id,
      const nlohmann::json& progress) override;
  void UpsertHostObservation(const naim::HostObservation& observation) override;
  void AppendEvent(const naim::EventRecord& event) override;
  void UpsertDiskRuntimeState(const naim::DiskRuntimeState& state) override;
  std::optional<naim::DiskRuntimeState> LoadDiskRuntimeState(
      const std::string& disk_name,
      const std::string& node_name) override;

 private:
  naim::ControllerStore store_;
};

}  // namespace naim::hostd
