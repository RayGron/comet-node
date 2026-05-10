#include "plane/plane_mutation_service.h"

#include <iostream>

#include "naim/state/desired_state_v2_renderer.h"
#include "naim/state/sqlite_store.h"
#include "naim/state/state_json.h"
#include "skills/plane_skills_target_resolver.h"

namespace naim::controller {

namespace {

constexpr int kSkillsRuntimeSyncTimeoutMs = 5000;

void SyncSkillsRuntimeAfterDesiredStateApply(
    const std::string& db_path,
    const naim::DesiredState& desired_state) {
  if (!desired_state.skills.has_value() || !desired_state.skills->enabled) {
    return;
  }
  ControllerStore store(db_path);
  store.Initialize();
  const auto plane = store.LoadPlane(desired_state.plane_name);
  if (!plane.has_value() || plane->applied_generation <= 0 || plane->state != "running") {
    return;
  }
  const auto target = PlaneSkillsTargetResolver::ResolvePlaneLocalTarget(desired_state);
  if (!target.has_value()) {
    return;
  }
  try {
    const auto response = PlaneSkillsTargetResolver::SendPlaneLocalRequest(
        db_path,
        desired_state.plane_name,
        *target,
        "POST",
        "/v1/sync",
        "{}",
        PlaneSkillsTargetResolver::DefaultJsonHeaders(),
        kSkillsRuntimeSyncTimeoutMs);
    if (response.status_code < 200 || response.status_code >= 300) {
      std::cerr << "[naim-controller] skills runtime sync warning plane="
                << desired_state.plane_name << " status=" << response.status_code
                << "\n";
    }
  } catch (const std::exception& error) {
    std::cerr << "[naim-controller] skills runtime sync warning plane="
              << desired_state.plane_name << " error=" << error.what() << "\n";
  }
}

}  // namespace

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
        const auto payload = nlohmann::json::parse(desired_state_json);
        const bool desired_state_v2 = source_label == "api/v2" || payload.value("version", 0) == 2;
        const naim::DesiredState desired_state = desired_state_v2
            ? naim::DesiredStateV2Renderer::Render(payload)
            : naim::DeserializeDesiredStateJson(desired_state_json);
        if (expected_plane_name.has_value() &&
            desired_state.plane_name != *expected_plane_name) {
          throw std::runtime_error(
              "plane name mismatch: expected '" + *expected_plane_name +
              "' but payload contains '" + desired_state.plane_name + "'");
        }
        const int result = deps_.apply_desired_state(
            db_path,
            desired_state,
            artifacts_root,
            source_label);
        if (result == 0 && deps_.enable_runtime_sync_after_apply) {
          SyncSkillsRuntimeAfterDesiredStateApply(db_path, desired_state);
        }
        return result;
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

}  // namespace naim::controller
