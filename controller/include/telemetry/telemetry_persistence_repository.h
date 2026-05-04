#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "naim/runtime/runtime_status.h"
#include "telemetry/telemetry_persistence_schema.h"
#include "telemetry/telemetry_persistence_status_builder.h"
#include "telemetry/telemetry_state_types.h"

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
  std::int64_t SafeSequenceForSqlite(std::uint64_t sequence) const;
  TelemetryPersistenceSchema schema_;
  TelemetryPersistenceStatusBuilder status_builder_;
};

}  // namespace naim::controller
