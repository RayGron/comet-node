#include "telemetry/telemetry_persistence_repository.h"

#include <algorithm>
#include <limits>

#include "naim/state/sqlite_statement.h"
#include "telemetry/telemetry_sqlite_connection.h"

namespace naim::controller {

std::vector<naim::HostTelemetryFrame> TelemetryPersistenceRepository::Configure(
    TelemetryPersistenceState& state,
    const std::string& db_path,
    const std::size_t retention_capacity) const {
  state.enabled = false;
  state.db_path = db_path;
  state.retention_capacity = std::max<std::size_t>(1, retention_capacity);
  try {
    const auto frames = LoadFrames(db_path, state.retention_capacity);
    state.loaded_frames_total += frames.size();
    state.enabled = true;
    state.last_error.clear();
    return frames;
  } catch (const std::exception& error) {
    state.error_count += 1;
    state.last_error = error.what();
  }
  return {};
}

void TelemetryPersistenceRepository::PersistFrame(
    TelemetryPersistenceState& state,
    const naim::HostTelemetryFrame& frame) const {
  if (!state.enabled || state.db_path.empty()) {
    return;
  }
  try {
    TelemetrySqliteConnection connection(state.db_path);
    schema_.Ensure(connection.get());
    {
      naim::SqliteStatement statement(
          connection.get(),
          "INSERT INTO telemetry_ring_buffer("
          "sequence, node_name, plane_name, schema_version, sampled_at, frame_json) "
          "VALUES(?, ?, ?, ?, ?, ?) "
          "ON CONFLICT(sequence) DO UPDATE SET "
          "node_name=excluded.node_name,"
          "plane_name=excluded.plane_name,"
          "schema_version=excluded.schema_version,"
          "sampled_at=excluded.sampled_at,"
          "frame_json=excluded.frame_json");
      statement.BindInt64(1, SafeSequenceForSqlite(frame.sequence));
      statement.BindText(2, frame.node_name);
      statement.BindText(3, frame.plane_name);
      statement.BindText(4, frame.schema_version);
      statement.BindText(5, frame.sampled_at);
      statement.BindText(6, naim::SerializeHostTelemetryFrameJson(frame));
      statement.StepDone();
    }
    {
      naim::SqliteStatement prune(
          connection.get(),
          "DELETE FROM telemetry_ring_buffer "
          "WHERE sequence NOT IN ("
          "SELECT sequence FROM telemetry_ring_buffer "
          "ORDER BY sequence DESC LIMIT ?)");
      prune.BindInt64(1, static_cast<std::int64_t>(state.retention_capacity));
      prune.StepDone();
      const int changed = sqlite3_changes(connection.get());
      if (changed > 0) {
        state.pruned_frames_total += static_cast<std::uint64_t>(changed);
      }
    }
    state.persisted_frames_total += 1;
    state.last_error.clear();
  } catch (const std::exception& error) {
    state.error_count += 1;
    state.last_error = error.what();
  }
}

std::vector<naim::HostTelemetryFrame> TelemetryPersistenceRepository::LoadFrames(
    const std::string& db_path,
    const std::size_t retention_capacity) const {
  TelemetrySqliteConnection connection(db_path);
  schema_.Ensure(connection.get());
  naim::SqliteStatement statement(
      connection.get(),
      "SELECT frame_json FROM telemetry_ring_buffer "
      "ORDER BY sequence DESC LIMIT ?");
  statement.BindInt64(1, static_cast<std::int64_t>(retention_capacity));
  std::vector<std::string> payloads;
  while (statement.StepRow()) {
    const unsigned char* text = sqlite3_column_text(statement.raw(), 0);
    if (text != nullptr) {
      payloads.emplace_back(reinterpret_cast<const char*>(text));
    }
  }
  std::vector<naim::HostTelemetryFrame> frames;
  for (auto it = payloads.rbegin(); it != payloads.rend(); ++it) {
    frames.push_back(naim::DeserializeHostTelemetryFrameJson(*it));
  }
  return frames;
}

nlohmann::json TelemetryPersistenceRepository::BuildStatus(
    const TelemetryPersistenceState& state) const {
  return status_builder_.Build(state);
}

std::int64_t TelemetryPersistenceRepository::SafeSequenceForSqlite(
    const std::uint64_t sequence) const {
  return static_cast<std::int64_t>(std::min<std::uint64_t>(
      sequence,
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())));
}

}  // namespace naim::controller
