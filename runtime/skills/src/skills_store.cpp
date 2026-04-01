#include "skills/skills_store.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <random>
#include <sstream>
#include <stdexcept>

#include "comet/state/sqlite_statement.h"

namespace comet::skills {

namespace {

using nlohmann::json;

std::string ToColumnText(sqlite3_stmt* statement, int column_index) {
  const unsigned char* text = sqlite3_column_text(statement, column_index);
  return text == nullptr ? std::string{} : std::string(reinterpret_cast<const char*>(text));
}

void Exec(sqlite3* db, const std::string& sql) {
  char* error_message = nullptr;
  const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error_message);
  if (rc != SQLITE_OK) {
    const std::string message =
        error_message == nullptr ? "unknown sqlite exec error" : error_message;
    if (error_message != nullptr) {
      sqlite3_free(error_message);
    }
    throw std::runtime_error(message);
  }
}

std::string FormatHex(std::uint64_t value, int width) {
  std::ostringstream out;
  out << std::hex;
  out.width(width);
  out.fill('0');
  out << value;
  return out.str();
}

}  // namespace

ApiError::ApiError(int status, std::string code, std::string message)
    : status_(status), code_(std::move(code)), message_(std::move(message)) {}

const char* ApiError::what() const noexcept {
  return message_.c_str();
}

int ApiError::status() const {
  return status_;
}

const std::string& ApiError::code() const {
  return code_;
}

const std::string& ApiError::message() const {
  return message_;
}

SkillsStore::SkillsStore(const std::filesystem::path& db_path) : db_path_(db_path) {
  std::filesystem::create_directories(db_path_.parent_path());
  if (sqlite3_open(db_path_.string().c_str(), &db_) != SQLITE_OK) {
    const std::string message = db_ == nullptr ? "unknown sqlite open error" : sqlite3_errmsg(db_);
    if (db_ != nullptr) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
    throw std::runtime_error("failed to open skills db: " + message);
  }
  sqlite3_busy_timeout(db_, 10000);
  Exec(db_, "PRAGMA foreign_keys = ON");
  Exec(db_, "PRAGMA journal_mode = WAL");
  InitializeSchema();
}

SkillsStore::~SkillsStore() {
  if (db_ != nullptr) {
    sqlite3_close(db_);
  }
}

void SkillsStore::InitializeSchema() {
  Exec(
      db_,
      R"SQL(
        CREATE TABLE IF NOT EXISTS skills (
          id TEXT PRIMARY KEY,
          name TEXT NOT NULL,
          description TEXT NOT NULL,
          content TEXT NOT NULL,
          enabled INTEGER NOT NULL DEFAULT 1,
          created_at TEXT NOT NULL,
          updated_at TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS skill_session_bindings (
          skill_id TEXT NOT NULL,
          session_id TEXT NOT NULL,
          PRIMARY KEY (skill_id, session_id),
          FOREIGN KEY (skill_id) REFERENCES skills(id) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS skill_links (
          skill_id TEXT NOT NULL,
          link TEXT NOT NULL,
          PRIMARY KEY (skill_id, link),
          FOREIGN KEY (skill_id) REFERENCES skills(id) ON DELETE CASCADE
        );
      )SQL");
}

std::string SkillsStore::UtcNow() {
  const std::time_t now = std::time(nullptr);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &now);
#else
  gmtime_r(&now, &utc);
#endif
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return buffer;
}

std::string SkillsStore::GenerateUuid7Like() {
  static std::mt19937_64 rng(std::random_device{}());
  const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now());
  const std::uint64_t ts_ms =
      static_cast<std::uint64_t>(now.time_since_epoch().count()) & ((1ULL << 48) - 1);
  const std::uint16_t rand_a = static_cast<std::uint16_t>(rng() & 0x0fffU);
  const std::uint64_t rand_b = rng() & ((1ULL << 62) - 1);

  const std::uint32_t part1 = static_cast<std::uint32_t>((ts_ms >> 16) & 0xffffffffULL);
  const std::uint16_t part2 = static_cast<std::uint16_t>(ts_ms & 0xffffULL);
  const std::uint16_t part3 = static_cast<std::uint16_t>(0x7000U | rand_a);
  const std::uint16_t part4 =
      static_cast<std::uint16_t>(0x8000U | static_cast<std::uint16_t>((rand_b >> 48) & 0x3fffU));
  const std::uint64_t part5 = rand_b & 0xffffffffffffULL;

  return FormatHex(part1, 8) + "-" + FormatHex(part2, 4) + "-" + FormatHex(part3, 4) + "-" +
         FormatHex(part4, 4) + "-" + FormatHex(part5, 12);
}

std::vector<std::string> SkillsStore::NormalizeStringList(
    const json& value,
    const std::string& field_name,
    bool allow_empty) {
  if (value.is_null()) {
    return {};
  }
  if (!value.is_array()) {
    throw ApiError(400, "invalid_request", field_name + " must be an array");
  }
  std::vector<std::string> result;
  for (const auto& item : value) {
    if (!item.is_string()) {
      throw ApiError(400, "invalid_request", field_name + " items must be strings");
    }
    std::string normalized = item.get<std::string>();
    normalized.erase(
        normalized.begin(),
        std::find_if(normalized.begin(), normalized.end(), [](unsigned char ch) {
          return !std::isspace(ch);
        }));
    normalized.erase(
        std::find_if(normalized.rbegin(), normalized.rend(), [](unsigned char ch) {
          return !std::isspace(ch);
        }).base(),
        normalized.end());
    if (normalized.empty()) {
      if (allow_empty) {
        continue;
      }
      throw ApiError(400, "invalid_request", field_name + " items must not be empty");
    }
    if (std::find(result.begin(), result.end(), normalized) == result.end()) {
      result.push_back(std::move(normalized));
    }
  }
  return result;
}

json SkillsStore::NormalizeSkillPayload(const json& payload, bool partial) {
  if (!payload.is_object()) {
    throw ApiError(400, "invalid_request", "request body must be a JSON object");
  }
  json normalized = json::object();
  if (!partial) {
    for (const auto* key : {"name", "description", "content"}) {
      if (!payload.contains(key)) {
        throw ApiError(400, "invalid_request", std::string(key) + " is required");
      }
    }
  }

  for (const auto* key : {"name", "description", "content"}) {
    if (payload.contains(key)) {
      if (!payload.at(key).is_string()) {
        throw ApiError(400, "invalid_request", std::string(key) + " must be a non-empty string");
      }
      std::string value = payload.at(key).get<std::string>();
      value.erase(
          value.begin(),
          std::find_if(value.begin(), value.end(), [](unsigned char ch) { return !std::isspace(ch); }));
      value.erase(
          std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(),
          value.end());
      if (value.empty()) {
        throw ApiError(400, "invalid_request", std::string(key) + " must be a non-empty string");
      }
      normalized[key] = value;
    }
  }

  if (payload.contains("enabled")) {
    if (!payload.at("enabled").is_boolean()) {
      throw ApiError(400, "invalid_request", "enabled must be a boolean");
    }
    normalized["enabled"] = payload.at("enabled").get<bool>();
  } else if (!partial) {
    normalized["enabled"] = true;
  }

  if (payload.contains("session_ids")) {
    normalized["session_ids"] = NormalizeStringList(payload.at("session_ids"), "session_ids");
  } else if (!partial) {
    normalized["session_ids"] = json::array();
  }

  if (payload.contains("comet_links")) {
    normalized["comet_links"] = NormalizeStringList(payload.at("comet_links"), "comet_links");
  } else if (!partial) {
    normalized["comet_links"] = json::array();
  }

  return normalized;
}

SkillsStore::SkillArrays SkillsStore::LoadSkillArraysLocked(const std::string& skill_id) {
  SkillArrays arrays;
  {
    SqliteStatement statement(
        db_,
        "SELECT session_id FROM skill_session_bindings WHERE skill_id = ? ORDER BY session_id");
    statement.BindText(1, skill_id);
    while (statement.StepRow()) {
      arrays.session_ids.push_back(ToColumnText(statement.raw(), 0));
    }
  }
  {
    SqliteStatement statement(
        db_,
        "SELECT link FROM skill_links WHERE skill_id = ? ORDER BY link");
    statement.BindText(1, skill_id);
    while (statement.StepRow()) {
      arrays.comet_links.push_back(ToColumnText(statement.raw(), 0));
    }
  }
  return arrays;
}

json SkillsStore::SkillFromStatementLocked(sqlite3_stmt* statement) {
  const std::string skill_id = ToColumnText(statement, 0);
  const SkillArrays arrays = LoadSkillArraysLocked(skill_id);
  return json{
      {"id", skill_id},
      {"name", ToColumnText(statement, 1)},
      {"description", ToColumnText(statement, 2)},
      {"content", ToColumnText(statement, 3)},
      {"enabled", sqlite3_column_int(statement, 4) != 0},
      {"session_ids", arrays.session_ids},
      {"comet_links", arrays.comet_links},
      {"created_at", ToColumnText(statement, 5)},
      {"updated_at", ToColumnText(statement, 6)},
  };
}

void SkillsStore::ReplaceArraysLocked(
    const std::string& skill_id,
    const std::vector<std::string>& session_ids,
    const std::vector<std::string>& comet_links) {
  {
    SqliteStatement clear_sessions(db_, "DELETE FROM skill_session_bindings WHERE skill_id = ?");
    clear_sessions.BindText(1, skill_id);
    clear_sessions.StepDone();
  }
  {
    SqliteStatement clear_links(db_, "DELETE FROM skill_links WHERE skill_id = ?");
    clear_links.BindText(1, skill_id);
    clear_links.StepDone();
  }
  for (const auto& session_id : session_ids) {
    SqliteStatement insert(db_, "INSERT INTO skill_session_bindings(skill_id, session_id) VALUES (?, ?)");
    insert.BindText(1, skill_id);
    insert.BindText(2, session_id);
    insert.StepDone();
  }
  for (const auto& link : comet_links) {
    SqliteStatement insert(db_, "INSERT INTO skill_links(skill_id, link) VALUES (?, ?)");
    insert.BindText(1, skill_id);
    insert.BindText(2, link);
    insert.StepDone();
  }
}

std::optional<json> SkillsStore::FindSkillLocked(const std::string& skill_id, bool enabled_only) {
  std::string sql = "SELECT id, name, description, content, enabled, created_at, updated_at "
                    "FROM skills WHERE id = ?";
  if (enabled_only) {
    sql += " AND enabled = 1";
  }
  SqliteStatement statement(db_, sql);
  statement.BindText(1, skill_id);
  if (!statement.StepRow()) {
    return std::nullopt;
  }
  return SkillFromStatementLocked(statement.raw());
}

nlohmann::json SkillsStore::ListSkills() {
  std::lock_guard<std::mutex> lock(mutex_);
  json items = json::array();
  SqliteStatement statement(
      db_,
      "SELECT id, name, description, content, enabled, created_at, updated_at "
      "FROM skills ORDER BY updated_at DESC, name ASC");
  while (statement.StepRow()) {
    items.push_back(SkillFromStatementLocked(statement.raw()));
  }
  return items;
}

nlohmann::json SkillsStore::GetSkill(const std::string& skill_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto skill = FindSkillLocked(skill_id, false);
  if (!skill.has_value()) {
    throw ApiError(404, "skill_not_found", "skill '" + skill_id + "' not found");
  }
  return *skill;
}

nlohmann::json SkillsStore::CreateSkill(const json& payload) {
  const json normalized = NormalizeSkillPayload(payload, false);
  std::string skill_id;
  if (payload.contains("id")) {
    if (!payload.at("id").is_string()) {
      throw ApiError(400, "invalid_request", "id must be a non-empty string");
    }
    skill_id = payload.at("id").get<std::string>();
    if (skill_id.empty()) {
      throw ApiError(400, "invalid_request", "id must be a non-empty string");
    }
  } else {
    skill_id = GenerateUuid7Like();
  }

  const std::string now = UtcNow();
  json created;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    Exec(db_, "BEGIN IMMEDIATE TRANSACTION");
    try {
      SqliteStatement insert(
          db_,
          "INSERT INTO skills(id, name, description, content, enabled, created_at, updated_at) "
          "VALUES (?, ?, ?, ?, ?, ?, ?)");
      insert.BindText(1, skill_id);
      insert.BindText(2, normalized.at("name").get<std::string>());
      insert.BindText(3, normalized.at("description").get<std::string>());
      insert.BindText(4, normalized.at("content").get<std::string>());
      insert.BindInt(5, normalized.at("enabled").get<bool>() ? 1 : 0);
      insert.BindText(6, now);
      insert.BindText(7, now);
      insert.StepDone();
      ReplaceArraysLocked(
          skill_id,
          normalized.at("session_ids").get<std::vector<std::string>>(),
          normalized.at("comet_links").get<std::vector<std::string>>());
      created = *FindSkillLocked(skill_id, false);
      Exec(db_, "COMMIT");
    } catch (...) {
      Exec(db_, "ROLLBACK");
      throw;
    }
  }
  return created;
}

nlohmann::json SkillsStore::ReplaceSkill(
    const std::string& skill_id,
    const json& payload,
    bool partial) {
  const json update = NormalizeSkillPayload(payload, partial);
  json existing;
  bool found = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found_skill = FindSkillLocked(skill_id, false);
    if (found_skill.has_value()) {
      existing = *found_skill;
      found = true;
    }
  }
  if (!found) {
    if (partial) {
      throw ApiError(404, "skill_not_found", "skill '" + skill_id + "' not found");
    }
    json created_payload = payload;
    created_payload["id"] = skill_id;
    return CreateSkill(created_payload);
  }

  json merged = existing;
  for (auto it = update.begin(); it != update.end(); ++it) {
    merged[it.key()] = it.value();
  }
  merged["updated_at"] = UtcNow();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    Exec(db_, "BEGIN IMMEDIATE TRANSACTION");
    try {
      SqliteStatement statement(
          db_,
          "UPDATE skills SET name = ?, description = ?, content = ?, enabled = ?, updated_at = ? "
          "WHERE id = ?");
      statement.BindText(1, merged.at("name").get<std::string>());
      statement.BindText(2, merged.at("description").get<std::string>());
      statement.BindText(3, merged.at("content").get<std::string>());
      statement.BindInt(4, merged.at("enabled").get<bool>() ? 1 : 0);
      statement.BindText(5, merged.at("updated_at").get<std::string>());
      statement.BindText(6, skill_id);
      statement.StepDone();
      ReplaceArraysLocked(
          skill_id,
          merged.at("session_ids").get<std::vector<std::string>>(),
          merged.at("comet_links").get<std::vector<std::string>>());
      Exec(db_, "COMMIT");
    } catch (...) {
      Exec(db_, "ROLLBACK");
      throw;
    }
  }
  return GetSkill(skill_id);
}

void SkillsStore::DeleteSkill(const std::string& skill_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!FindSkillLocked(skill_id, false).has_value()) {
    throw ApiError(404, "skill_not_found", "skill '" + skill_id + "' not found");
  }
  SqliteStatement statement(db_, "DELETE FROM skills WHERE id = ?");
  statement.BindText(1, skill_id);
  statement.StepDone();
}

nlohmann::json SkillsStore::ResolveSkills(const json& payload) {
  if (!payload.is_object()) {
    throw ApiError(400, "invalid_request", "request body must be a JSON object");
  }
  std::optional<std::string> session_id;
  if (payload.contains("session_id") && !payload.at("session_id").is_null()) {
    if (!payload.at("session_id").is_string()) {
      throw ApiError(400, "invalid_request", "session_id must be a non-empty string");
    }
    const std::string raw = payload.at("session_id").get<std::string>();
    if (raw.empty()) {
      throw ApiError(400, "invalid_request", "session_id must be a non-empty string");
    }
    session_id = raw;
  }
  const std::vector<std::string> skill_ids =
      NormalizeStringList(payload.value("skill_ids", json::array()), "skill_ids");

  json explicit_skills = json::array();
  json session_skills = json::array();
  std::vector<std::string> seen;
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& skill_id : skill_ids) {
    const auto skill = FindSkillLocked(skill_id, true);
    if (!skill.has_value()) {
      throw ApiError(
          400,
          "invalid_skill_reference",
          "skill '" + skill_id + "' does not exist or is disabled");
    }
    json item = *skill;
    item["source"] = "explicit";
    explicit_skills.push_back(std::move(item));
    seen.push_back(skill_id);
  }

  if (session_id.has_value()) {
    SqliteStatement statement(
        db_,
        "SELECT s.id, s.name, s.description, s.content, s.enabled, s.created_at, s.updated_at "
        "FROM skills s "
        "JOIN skill_session_bindings ss ON ss.skill_id = s.id "
        "WHERE ss.session_id = ? AND s.enabled = 1 "
        "ORDER BY s.updated_at DESC, s.name ASC");
    statement.BindText(1, *session_id);
    while (statement.StepRow()) {
      const std::string found_id = ToColumnText(statement.raw(), 0);
      if (std::find(seen.begin(), seen.end(), found_id) != seen.end()) {
        continue;
      }
      json item = SkillFromStatementLocked(statement.raw());
      item["source"] = "session";
      session_skills.push_back(std::move(item));
      seen.push_back(found_id);
    }
  }

  json resolved = json::array();
  for (const auto& item : explicit_skills) {
    resolved.push_back(item);
  }
  for (const auto& item : session_skills) {
    resolved.push_back(item);
  }
  return json{
      {"skills", resolved},
      {"skills_session_id", session_id.has_value() ? json(*session_id) : json(nullptr)},
  };
}

}  // namespace comet::skills
