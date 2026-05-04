#pragma once

#include <string>

#include <sqlite3.h>

namespace naim::controller {

class TelemetryPersistenceSchema final {
 public:
  void Ensure(sqlite3* db) const;

 private:
  void Execute(sqlite3* db, const std::string& sql) const;
};

}  // namespace naim::controller
