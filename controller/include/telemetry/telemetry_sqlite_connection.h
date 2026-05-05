#pragma once

#include <string>

#include <sqlite3.h>

namespace naim::controller {

class TelemetrySqliteConnection final {
 public:
  explicit TelemetrySqliteConnection(const std::string& db_path);
  ~TelemetrySqliteConnection();

  TelemetrySqliteConnection(const TelemetrySqliteConnection&) = delete;
  TelemetrySqliteConnection& operator=(const TelemetrySqliteConnection&) = delete;

  sqlite3* get() const;

 private:
  sqlite3* db_ = nullptr;
};

}  // namespace naim::controller
