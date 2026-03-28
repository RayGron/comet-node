#include "infra/controller_health_service.h"

#include <filesystem>
#include <optional>
#include <string>

#include "comet/state/sqlite_store.h"

using nlohmann::json;

namespace comet::controller {

json ControllerHealthService::BuildPayload(const std::string& db_path) const {
  json payload{
      {"status", "ok"},
      {"service", "comet-controller"},
      {"db_path", db_path},
      {"db_exists", std::filesystem::exists(db_path)},
  };

  try {
    comet::ControllerStore store(db_path);
    store.Initialize();
    const auto generation = store.LoadDesiredGeneration();
    const auto desired_state = store.LoadDesiredState();
    const auto planes = store.LoadPlanes();
    payload["store_ready"] = true;
    payload["desired_generation"] =
        generation.has_value() ? json(*generation) : json(nullptr);
    payload["plane_name"] = desired_state.has_value()
                                ? json(desired_state->plane_name)
                                : json(nullptr);
    payload["plane_count"] = planes.size();
  } catch (const std::exception& error) {
    payload["store_ready"] = false;
    payload["error"] = error.what();
  }

  return payload;
}

}  // namespace comet::controller
