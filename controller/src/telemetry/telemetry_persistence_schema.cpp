#include "telemetry/telemetry_persistence_schema.h"

#include <stdexcept>

namespace naim::controller {

void TelemetryPersistenceSchema::Ensure(sqlite3* db) const {
  Execute(
      db,
      "CREATE TABLE IF NOT EXISTS telemetry_ring_buffer ("
      "sequence INTEGER PRIMARY KEY,"
      "node_name TEXT NOT NULL,"
      "plane_name TEXT,"
      "schema_version TEXT NOT NULL,"
      "sampled_at TEXT,"
      "frame_json TEXT NOT NULL,"
      "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)");
  Execute(
      db,
      "CREATE INDEX IF NOT EXISTS idx_telemetry_ring_buffer_plane_sequence "
      "ON telemetry_ring_buffer(plane_name, sequence)");
}

void TelemetryPersistenceSchema::Execute(sqlite3* db, const std::string& sql) const {
  char* error_message = nullptr;
  const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error_message);
  if (rc != SQLITE_OK) {
    std::string message = error_message != nullptr ? error_message : sqlite3_errmsg(db);
    sqlite3_free(error_message);
    throw std::runtime_error("sqlite exec failed: " + message);
  }
}

}  // namespace naim::controller
