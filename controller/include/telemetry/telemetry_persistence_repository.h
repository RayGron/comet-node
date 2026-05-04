#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "naim/runtime/runtime_status.h"
#include "telemetry/telemetry_live_store_types.h"

namespace naim::controller {

class TelemetryPersistenceRepository final {
 public:
  std::vector<naim::HostTelemetryFrame> Configure(
      TelemetryPersistenceState& state,
      const std::string& db_path,
      std::size_t retention_capacity) const;
  void PersistFrame(
      TelemetryPersistenceState& state,
      const naim::HostTelemetryFrame& frame) const;
  std::vector<naim::HostTelemetryFrame> LoadFrames(
      const std::string& db_path,
      std::size_t retention_capacity) const;
  nlohmann::json BuildStatus(const TelemetryPersistenceState& state) const;

 private:
  void EnsureSchema(sqlite3* db) const;
  void Execute(sqlite3* db, const std::string& sql) const;
  std::int64_t SafeSequenceForSqlite(std::uint64_t sequence) const;
};

}  // namespace naim::controller
