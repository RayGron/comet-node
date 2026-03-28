#include "plane/plane_mutation_service.h"

#include "comet/state/state_json.h"

namespace comet::controller {

PlaneMutationService::PlaneMutationService(Deps deps) : deps_(std::move(deps)) {}

ControllerActionResult PlaneMutationService::ExecuteUpsertPlaneStateAction(
    const std::string& db_path,
    const std::string& desired_state_json,
    const std::string& artifacts_root,
    const std::optional<std::string>& expected_plane_name,
    const std::string& source_label) const {
  return RunControllerActionResult(
      "upsert-plane-state",
      [&]() {
        const auto desired_state = comet::DeserializeDesiredStateJson(desired_state_json);
        if (expected_plane_name.has_value() &&
            desired_state.plane_name != *expected_plane_name) {
          throw std::runtime_error(
              "plane name mismatch: expected '" + *expected_plane_name +
              "' but payload contains '" + desired_state.plane_name + "'");
        }
        return deps_.apply_desired_state(
            db_path,
            desired_state,
            artifacts_root,
            source_label);
      });
}

ControllerActionResult PlaneMutationService::ExecuteStartPlaneAction(
    const std::string& db_path,
    const std::string& plane_name) const {
  PlaneService plane_service = deps_.make_plane_service(db_path);
  return RunControllerActionResult(
      "start-plane",
      [&]() { return plane_service.StartPlane(plane_name); });
}

ControllerActionResult PlaneMutationService::ExecuteStopPlaneAction(
    const std::string& db_path,
    const std::string& plane_name) const {
  PlaneService plane_service = deps_.make_plane_service(db_path);
  return RunControllerActionResult(
      "stop-plane",
      [&]() { return plane_service.StopPlane(plane_name); });
}

ControllerActionResult PlaneMutationService::ExecuteDeletePlaneAction(
    const std::string& db_path,
    const std::string& plane_name) const {
  PlaneService plane_service = deps_.make_plane_service(db_path);
  return RunControllerActionResult(
      "delete-plane",
      [&]() { return plane_service.DeletePlane(plane_name); });
}

}  // namespace comet::controller
