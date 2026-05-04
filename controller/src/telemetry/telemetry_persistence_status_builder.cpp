#include "telemetry/telemetry_persistence_status_builder.h"

namespace naim::controller {

nlohmann::json TelemetryPersistenceStatusBuilder::Build(
    const TelemetryPersistenceState& state) const {
  return nlohmann::json{
      {"enabled", state.enabled},
      {"backend", state.enabled ? "sqlite" : "memory"},
      {"db_path", state.db_path},
      {"retention_capacity", state.retention_capacity},
      {"loaded_frames_total", state.loaded_frames_total},
      {"persisted_frames_total", state.persisted_frames_total},
      {"pruned_frames_total", state.pruned_frames_total},
      {"error_count", state.error_count},
      {"last_error", state.last_error},
  };
}

}  // namespace naim::controller
