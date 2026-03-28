#pragma once

#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "comet/state/models.h"
#include "comet/state/sqlite_store.h"

namespace comet::controller {

class PlaneRegistryService {
 public:
  using PlaneDeleteFinalizer =
      std::function<bool(comet::ControllerStore&, const std::string&)>;
  using PlaneEventAppender = std::function<void(
      comet::ControllerStore&,
      const std::string&,
      const std::string&,
      const std::string&,
      const nlohmann::json&,
      const std::string&)>;
  using PlaneObservationFilter = std::function<std::vector<comet::HostObservation>(
      const std::vector<comet::HostObservation>&,
      const std::string&)>;
  using PlaneAppliedGenerationResolver = std::function<int(
      const comet::PlaneRecord&,
      const std::optional<comet::DesiredState>&,
      const std::optional<int>&,
      const std::vector<comet::HostObservation>&)>;
  using LatestAssignmentsByNodeBuilder = std::function<
      std::map<std::string, comet::HostAssignment>(
          const std::vector<comet::HostAssignment>&)>;

  struct Deps {
    PlaneDeleteFinalizer can_finalize_deleted_plane;
    PlaneEventAppender event_appender;
    PlaneObservationFilter filter_host_observations_for_plane;
    PlaneAppliedGenerationResolver compute_effective_applied_generation;
    LatestAssignmentsByNodeBuilder build_latest_assignments_by_node;
  };

  explicit PlaneRegistryService(Deps deps);

  nlohmann::json BuildPlanesPayload(const std::string& db_path) const;

 private:
  Deps deps_;
};

}  // namespace comet::controller
