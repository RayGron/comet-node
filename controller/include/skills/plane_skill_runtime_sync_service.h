#pragma once

#include <string>

#include "comet/state/models.h"

namespace comet::controller {

class PlaneSkillRuntimeSyncService final {
 public:
  bool SyncPlane(const std::string& db_path, const comet::DesiredState& desired_state) const;

 private:
  static bool IsReadyForSync(const std::string& db_path, const comet::DesiredState& desired_state);
};

}  // namespace comet::controller
