#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "app/controller_service_interfaces.h"
#include "comet/state/models.h"
#include "comet/planning/scheduling_policy.h"
#include "comet/state/sqlite_store.h"
#include "plane/plane_lifecycle_support.h"
#include "plane/plane_state_presentation_support.h"

namespace comet::controller {

class PlaneService : public IPlaneService {
 public:
  PlaneService(
      std::string db_path,
      std::shared_ptr<const PlaneStatePresentationSupport> state_presentation_support,
      std::shared_ptr<const PlaneLifecycleSupport> lifecycle_support);

  int ListPlanes() const override;
  int ShowPlane(const std::string& plane_name) const override;
  int StartPlane(const std::string& plane_name) const override;
  int StopPlane(const std::string& plane_name) const override;
  int DeletePlane(const std::string& plane_name) const override;

 private:
  bool FinalizeDeletedPlaneIfReady(
      comet::ControllerStore& store,
      const std::string& plane_name) const;
  std::string ResolveArtifactsRoot(
      comet::ControllerStore& store,
      const comet::PlaneRecord& plane,
      const std::string& plane_name) const;

  std::string db_path_;
  std::shared_ptr<const PlaneStatePresentationSupport> state_presentation_support_;
  std::shared_ptr<const PlaneLifecycleSupport> lifecycle_support_;
};

}  // namespace comet::controller
