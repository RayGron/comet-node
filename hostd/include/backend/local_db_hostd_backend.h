#pragma once

#include <string>

#include "backend/hostd_backend.h"

namespace comet::hostd {

class LocalDbHostdBackend final : public HostdBackend {
 public:
  explicit LocalDbHostdBackend(std::string db_path);

  std::optional<comet::HostAssignment> ClaimNextHostAssignment(
      const std::string& node_name) override;
  bool TransitionClaimedHostAssignment(
      int assignment_id,
      comet::HostAssignmentStatus status,
      const std::string& status_message) override;
  bool UpdateHostAssignmentProgress(
      int assignment_id,
      const nlohmann::json& progress) override;
  void UpsertHostObservation(const comet::HostObservation& observation) override;
  void AppendEvent(const comet::EventRecord& event) override;
  void UpsertDiskRuntimeState(const comet::DiskRuntimeState& state) override;
  std::optional<comet::DiskRuntimeState> LoadDiskRuntimeState(
      const std::string& disk_name,
      const std::string& node_name) override;

 private:
  comet::ControllerStore store_;
};

}  // namespace comet::hostd
