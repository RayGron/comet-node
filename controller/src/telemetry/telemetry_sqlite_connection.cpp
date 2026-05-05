#include "telemetry/telemetry_sqlite_connection.h"

#include <stdexcept>

namespace naim::controller {

TelemetrySqliteConnection::TelemetrySqliteConnection(const std::string& db_path) {
  if (sqlite3_open_v2(
          db_path.c_str(),
          &db_,
          SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
          nullptr) != SQLITE_OK) {
    std::string message = db_ != nullptr ? sqlite3_errmsg(db_) : "unknown";
    if (db_ != nullptr) {
      sqlite3_close(db_);
    }
    throw std::runtime_error("sqlite open failed: " + message);
  }
  sqlite3_busy_timeout(db_, 1000);
}

TelemetrySqliteConnection::~TelemetrySqliteConnection() {
  if (db_ != nullptr) {
    sqlite3_close(db_);
  }
}

sqlite3* TelemetrySqliteConnection::get() const {
  return db_;
}

}  // namespace naim::controller
