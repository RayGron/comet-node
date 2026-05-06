#include "naim/state/auth_repository.h"

#include "naim/state/sqlite_statement.h"

#include <stdexcept>

namespace naim {

namespace {

using Statement = SqliteStatement;

std::string ToColumnText(sqlite3_stmt* statement, int column_index) {
  const unsigned char* text = sqlite3_column_text(statement, column_index);
  if (text == nullptr) {
    return "";
  }
  return reinterpret_cast<const char*>(text);
}

std::optional<int> ToOptionalColumnInt(sqlite3_stmt* statement, int column_index) {
  if (sqlite3_column_type(statement, column_index) == SQLITE_NULL) {
    return std::nullopt;
  }
  return sqlite3_column_int(statement, column_index);
}

void Exec(sqlite3* db, const std::string& sql) {
  char* error_message = nullptr;
  const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error_message);
  if (rc != SQLITE_OK) {
    const std::string message = error_message == nullptr ? "unknown sqlite error" : error_message;
    sqlite3_free(error_message);
    throw std::runtime_error("sqlite exec failed: " + message);
  }
}

}  // namespace

AuthRepository::AuthRepository(sqlite3* db) : db_(db) {}

int AuthRepository::LoadUserCount() const {
  SqliteStatement statement(db_, "SELECT COUNT(*) FROM users;");
  if (!statement.StepRow()) {
    return 0;
  }
  return sqlite3_column_int(statement.raw(), 0);
}

std::optional<UserRecord> AuthRepository::LoadUserById(int user_id) const {
  SqliteStatement statement(
      db_,
      "SELECT id, username, role, password_hash, created_at, updated_at, last_login_at "
      "FROM users WHERE id = ?1;");
  statement.BindInt(1, user_id);
  if (!statement.StepRow()) {
    return std::nullopt;
  }
  return ReadUser(statement.raw());
}

std::optional<UserRecord> AuthRepository::LoadUserByUsername(
    const std::string& username) const {
  SqliteStatement statement(
      db_,
      "SELECT id, username, role, password_hash, created_at, updated_at, last_login_at "
      "FROM users WHERE username = ?1;");
  statement.BindText(1, username);
  if (!statement.StepRow()) {
    return std::nullopt;
  }
  return ReadUser(statement.raw());
}

std::vector<UserRecord> AuthRepository::LoadUsers() const {
  SqliteStatement statement(
      db_,
      "SELECT id, username, role, password_hash, created_at, updated_at, last_login_at "
      "FROM users ORDER BY id ASC;");
  std::vector<UserRecord> users;
  while (statement.StepRow()) {
    users.push_back(ReadUser(statement.raw()));
  }
  return users;
}

UserRecord AuthRepository::CreateBootstrapAdmin(
    const std::string& username,
    const std::string& password_hash) {
  Exec(db_, "BEGIN IMMEDIATE;");
  try {
    Statement count_statement(db_, "SELECT COUNT(*) FROM users;");
    if (!count_statement.StepRow()) {
      throw std::runtime_error("failed to count users");
    }
    if (sqlite3_column_int(count_statement.raw(), 0) != 0) {
      throw std::runtime_error(
          "bootstrap admin can be created only when user base is empty");
    }
    Statement insert_statement(
        db_,
        "INSERT INTO users(username, role, password_hash, updated_at) "
        "VALUES (?1, 'admin', ?2, CURRENT_TIMESTAMP);");
    insert_statement.BindText(1, username);
    insert_statement.BindText(2, password_hash);
    insert_statement.StepDone();
    const int user_id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    Exec(db_, "COMMIT;");
    return *LoadUserById(user_id);
  } catch (...) {
    Exec(db_, "ROLLBACK;");
    throw;
  }
}

UserRecord AuthRepository::CreateInvitedUser(
    const std::string& invite_token,
    const std::string& username,
    const std::string& password_hash) {
  Exec(db_, "BEGIN IMMEDIATE;");
  try {
    Statement invite_statement(
        db_,
        "SELECT id FROM registration_invites "
        "WHERE token = ?1 AND revoked_at = '' AND used_at = '' "
        "AND expires_at >= CURRENT_TIMESTAMP;");
    invite_statement.BindText(1, invite_token);
    if (!invite_statement.StepRow()) {
      throw std::runtime_error("invite token is missing, expired, or already used");
    }
    const int invite_id = sqlite3_column_int(invite_statement.raw(), 0);
    Statement insert_statement(
        db_,
        "INSERT INTO users(username, role, password_hash, updated_at) "
        "VALUES (?1, 'user', ?2, CURRENT_TIMESTAMP);");
    insert_statement.BindText(1, username);
    insert_statement.BindText(2, password_hash);
    insert_statement.StepDone();
    const int user_id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    Statement update_invite_statement(
        db_,
        "UPDATE registration_invites "
        "SET used_by_user_id = ?2, used_at = CURRENT_TIMESTAMP "
        "WHERE id = ?1;");
    update_invite_statement.BindInt(1, invite_id);
    update_invite_statement.BindInt(2, user_id);
    update_invite_statement.StepDone();
    Exec(db_, "COMMIT;");
    return *LoadUserById(user_id);
  } catch (...) {
    Exec(db_, "ROLLBACK;");
    throw;
  }
}

void AuthRepository::UpdateUserLastLoginAt(
    int user_id,
    const std::string& last_login_at) {
  Statement statement(
      db_,
      "UPDATE users SET last_login_at = ?2, updated_at = CURRENT_TIMESTAMP WHERE id = ?1;");
  statement.BindInt(1, user_id);
  statement.BindText(2, last_login_at);
  statement.StepDone();
}

void AuthRepository::InsertWebAuthnCredential(
    const WebAuthnCredentialRecord& credential) {
  Statement statement(
      db_,
      "INSERT INTO webauthn_credentials("
      " user_id, credential_id, public_key, counter, transports_json, updated_at"
      ") VALUES (?1, ?2, ?3, ?4, ?5, CURRENT_TIMESTAMP);");
  statement.BindInt(1, credential.user_id);
  statement.BindText(2, credential.credential_id);
  statement.BindText(3, credential.public_key);
  statement.BindInt(4, static_cast<int>(credential.counter));
  statement.BindText(5, credential.transports_json);
  statement.StepDone();
}

void AuthRepository::UpdateWebAuthnCredentialCounter(
    const std::string& credential_id,
    std::uint32_t counter,
    const std::string& last_used_at) {
  Statement statement(
      db_,
      "UPDATE webauthn_credentials "
      "SET counter = ?2, last_used_at = ?3, updated_at = CURRENT_TIMESTAMP "
      "WHERE credential_id = ?1;");
  statement.BindText(1, credential_id);
  statement.BindInt(2, static_cast<int>(counter));
  statement.BindText(3, last_used_at);
  statement.StepDone();
}

std::vector<WebAuthnCredentialRecord> AuthRepository::LoadWebAuthnCredentialsForUser(
    int user_id) const {
  Statement statement(
      db_,
      "SELECT id, user_id, credential_id, public_key, counter, transports_json, "
      "created_at, updated_at, last_used_at "
      "FROM webauthn_credentials WHERE user_id = ?1 ORDER BY id ASC;");
  statement.BindInt(1, user_id);
  std::vector<WebAuthnCredentialRecord> credentials;
  while (statement.StepRow()) {
    credentials.push_back(ReadWebAuthnCredential(statement.raw()));
  }
  return credentials;
}

std::optional<WebAuthnCredentialRecord> AuthRepository::LoadWebAuthnCredentialById(
    const std::string& credential_id) const {
  Statement statement(
      db_,
      "SELECT id, user_id, credential_id, public_key, counter, transports_json, "
      "created_at, updated_at, last_used_at "
      "FROM webauthn_credentials WHERE credential_id = ?1;");
  statement.BindText(1, credential_id);
  if (!statement.StepRow()) {
    return std::nullopt;
  }
  return ReadWebAuthnCredential(statement.raw());
}

RegistrationInviteRecord AuthRepository::CreateRegistrationInvite(
    int created_by_user_id,
    const std::string& token,
    const std::string& expires_at) {
  Statement statement(
      db_,
      "INSERT INTO registration_invites(token, created_by_user_id, expires_at) "
      "VALUES (?1, ?2, ?3);");
  statement.BindText(1, token);
  statement.BindInt(2, created_by_user_id);
  statement.BindText(3, expires_at);
  statement.StepDone();
  return *LoadRegistrationInviteByToken(token);
}

std::optional<RegistrationInviteRecord> AuthRepository::LoadRegistrationInviteByToken(
    const std::string& token) const {
  Statement statement(
      db_,
      "SELECT id, token, created_by_user_id, expires_at, created_at, "
      "used_by_user_id, used_at, revoked_at "
      "FROM registration_invites WHERE token = ?1;");
  statement.BindText(1, token);
  if (!statement.StepRow()) {
    return std::nullopt;
  }
  return ReadRegistrationInvite(statement.raw());
}

std::vector<RegistrationInviteRecord> AuthRepository::LoadActiveRegistrationInvites() const {
  Statement statement(
      db_,
      "SELECT id, token, created_by_user_id, expires_at, created_at, "
      "used_by_user_id, used_at, revoked_at "
      "FROM registration_invites "
      "WHERE revoked_at = '' AND used_at = '' AND expires_at >= CURRENT_TIMESTAMP "
      "ORDER BY created_at DESC, id DESC;");
  std::vector<RegistrationInviteRecord> invites;
  while (statement.StepRow()) {
    invites.push_back(ReadRegistrationInvite(statement.raw()));
  }
  return invites;
}

bool AuthRepository::MarkRegistrationInviteUsed(
    const std::string& token,
    int used_by_user_id,
    const std::string& used_at) {
  Statement statement(
      db_,
      "UPDATE registration_invites "
      "SET used_by_user_id = ?2, used_at = ?3 "
      "WHERE token = ?1 AND revoked_at = '' AND used_at = '' "
      "AND expires_at >= CURRENT_TIMESTAMP;");
  statement.BindText(1, token);
  statement.BindInt(2, used_by_user_id);
  statement.BindText(3, used_at);
  statement.StepDone();
  return sqlite3_changes(db_) > 0;
}

bool AuthRepository::RevokeRegistrationInvite(
    int invite_id,
    const std::string& revoked_at) {
  Statement statement(
      db_,
      "UPDATE registration_invites "
      "SET revoked_at = ?2 "
      "WHERE id = ?1 AND revoked_at = '' AND used_at = '';");
  statement.BindInt(1, invite_id);
  statement.BindText(2, revoked_at);
  statement.StepDone();
  return sqlite3_changes(db_) > 0;
}

void AuthRepository::InsertUserSshKey(const UserSshKeyRecord& ssh_key) {
  Statement statement(
      db_,
      "INSERT INTO user_ssh_keys(user_id, label, public_key, fingerprint) "
      "VALUES (?1, ?2, ?3, ?4);");
  statement.BindInt(1, ssh_key.user_id);
  statement.BindText(2, ssh_key.label);
  statement.BindText(3, ssh_key.public_key);
  statement.BindText(4, ssh_key.fingerprint);
  statement.StepDone();
}

std::vector<UserSshKeyRecord> AuthRepository::LoadActiveUserSshKeys(int user_id) const {
  Statement statement(
      db_,
      "SELECT id, user_id, label, public_key, fingerprint, created_at, "
      "revoked_at, last_used_at "
      "FROM user_ssh_keys WHERE user_id = ?1 AND revoked_at = '' ORDER BY id ASC;");
  statement.BindInt(1, user_id);
  std::vector<UserSshKeyRecord> ssh_keys;
  while (statement.StepRow()) {
    ssh_keys.push_back(ReadUserSshKey(statement.raw()));
  }
  return ssh_keys;
}

std::optional<UserSshKeyRecord> AuthRepository::LoadActiveUserSshKeyByFingerprint(
    int user_id,
    const std::string& fingerprint) const {
  Statement statement(
      db_,
      "SELECT id, user_id, label, public_key, fingerprint, created_at, "
      "revoked_at, last_used_at "
      "FROM user_ssh_keys WHERE user_id = ?1 AND fingerprint = ?2 AND revoked_at = '';");
  statement.BindInt(1, user_id);
  statement.BindText(2, fingerprint);
  if (!statement.StepRow()) {
    return std::nullopt;
  }
  return ReadUserSshKey(statement.raw());
}

std::optional<UserSshKeyRecord> AuthRepository::LoadActiveUserSshKeyById(
    int ssh_key_id) const {
  Statement statement(
      db_,
      "SELECT id, user_id, label, public_key, fingerprint, created_at, "
      "revoked_at, last_used_at "
      "FROM user_ssh_keys WHERE id = ?1 AND revoked_at = '';");
  statement.BindInt(1, ssh_key_id);
  if (!statement.StepRow()) {
    return std::nullopt;
  }
  return ReadUserSshKey(statement.raw());
}

bool AuthRepository::RevokeUserSshKey(
    int ssh_key_id,
    const std::string& revoked_at) {
  Statement statement(
      db_,
      "UPDATE user_ssh_keys SET revoked_at = ?2 WHERE id = ?1 AND revoked_at = '';");
  statement.BindInt(1, ssh_key_id);
  statement.BindText(2, revoked_at);
  statement.StepDone();
  return sqlite3_changes(db_) > 0;
}

void AuthRepository::TouchUserSshKey(
    int ssh_key_id,
    const std::string& last_used_at) {
  Statement statement(
      db_,
      "UPDATE user_ssh_keys SET last_used_at = ?2 WHERE id = ?1;");
  statement.BindInt(1, ssh_key_id);
  statement.BindText(2, last_used_at);
  statement.StepDone();
}

void AuthRepository::InsertAuthSession(const AuthSessionRecord& session) {
  Statement statement(
      db_,
      "INSERT INTO auth_sessions(token, user_id, session_kind, plane_name, "
      "expires_at, last_used_at) "
      "VALUES (?1, ?2, ?3, ?4, ?5, ?6);");
  statement.BindText(1, session.token);
  statement.BindInt(2, session.user_id);
  statement.BindText(3, session.session_kind);
  statement.BindText(4, session.plane_name);
  statement.BindText(5, session.expires_at);
  statement.BindText(6, session.last_used_at);
  statement.StepDone();
}

std::optional<AuthSessionRecord> AuthRepository::LoadActiveAuthSession(
    const std::string& token,
    const std::optional<std::string>& session_kind,
    const std::optional<std::string>& plane_name) const {
  std::string sql =
      "SELECT token, user_id, session_kind, plane_name, expires_at, created_at, "
      "revoked_at, last_used_at "
      "FROM auth_sessions WHERE token = ?1 AND revoked_at = '' "
      "AND expires_at >= CURRENT_TIMESTAMP";
  if (session_kind.has_value()) {
    sql += " AND session_kind = ?2";
  }
  if (plane_name.has_value()) {
    sql += session_kind.has_value() ? " AND plane_name = ?3" : " AND plane_name = ?2";
  }
  sql += ";";
  Statement statement(db_, sql);
  statement.BindText(1, token);
  int next_index = 2;
  if (session_kind.has_value()) {
    statement.BindText(next_index++, *session_kind);
  }
  if (plane_name.has_value()) {
    statement.BindText(next_index++, *plane_name);
  }
  if (!statement.StepRow()) {
    return std::nullopt;
  }
  return ReadAuthSession(statement.raw());
}

bool AuthRepository::RevokeAuthSession(
    const std::string& token,
    const std::string& revoked_at) {
  Statement statement(
      db_,
      "UPDATE auth_sessions SET revoked_at = ?2 WHERE token = ?1 AND revoked_at = '';");
  statement.BindText(1, token);
  statement.BindText(2, revoked_at);
  statement.StepDone();
  return sqlite3_changes(db_) > 0;
}

bool AuthRepository::TouchAuthSession(
    const std::string& token,
    const std::string& last_used_at) {
  Statement statement(
      db_,
      "UPDATE auth_sessions SET last_used_at = ?2 WHERE token = ?1;");
  statement.BindText(1, token);
  statement.BindText(2, last_used_at);
  statement.StepDone();
  return sqlite3_changes(db_) > 0;
}

void AuthRepository::UpsertSecuredConnectionUser(
    const SecuredConnectionUserRecord& user) {
  Statement statement(
      db_,
      "INSERT INTO secured_connection_users("
      "id, name, public_key, fingerprint, search_text, last_authorized_at, updated_at, revoked_at"
      ") VALUES (?1, ?2, ?3, ?4, ?5, ?6, CURRENT_TIMESTAMP, ?7) "
      "ON CONFLICT(id) DO UPDATE SET "
      "name = excluded.name, public_key = excluded.public_key, "
      "fingerprint = excluded.fingerprint, search_text = excluded.search_text, "
      "updated_at = CURRENT_TIMESTAMP, revoked_at = excluded.revoked_at;");
  statement.BindText(1, user.id);
  statement.BindText(2, user.name);
  statement.BindText(3, user.public_key);
  statement.BindText(4, user.fingerprint);
  statement.BindText(5, user.search_text);
  statement.BindText(6, user.last_authorized_at);
  statement.BindText(7, user.revoked_at);
  statement.StepDone();
}

std::optional<SecuredConnectionUserRecord>
AuthRepository::LoadActiveSecuredConnectionUser(const std::string& user_id) const {
  Statement statement(
      db_,
      "SELECT id, name, public_key, fingerprint, search_text, last_authorized_at, "
      "created_at, updated_at, revoked_at "
      "FROM secured_connection_users WHERE id = ?1 AND revoked_at = '';");
  statement.BindText(1, user_id);
  if (!statement.StepRow()) {
    return std::nullopt;
  }
  return ReadSecuredConnectionUser(statement.raw());
}

std::optional<SecuredConnectionUserRecord>
AuthRepository::LoadActiveSecuredConnectionUserByNameAndFingerprint(
    const std::string& name,
    const std::string& fingerprint) const {
  Statement statement(
      db_,
      "SELECT id, name, public_key, fingerprint, search_text, last_authorized_at, "
      "created_at, updated_at, revoked_at "
      "FROM secured_connection_users "
      "WHERE name = ?1 AND fingerprint = ?2 AND revoked_at = '';");
  statement.BindText(1, name);
  statement.BindText(2, fingerprint);
  if (!statement.StepRow()) {
    return std::nullopt;
  }
  return ReadSecuredConnectionUser(statement.raw());
}

std::optional<SecuredConnectionUserRecord>
AuthRepository::LoadActiveSecuredConnectionUserByFingerprint(
    const std::string& fingerprint) const {
  Statement statement(
      db_,
      "SELECT id, name, public_key, fingerprint, search_text, last_authorized_at, "
      "created_at, updated_at, revoked_at "
      "FROM secured_connection_users "
      "WHERE fingerprint = ?1 AND revoked_at = '';");
  statement.BindText(1, fingerprint);
  if (!statement.StepRow()) {
    return std::nullopt;
  }
  return ReadSecuredConnectionUser(statement.raw());
}

std::vector<SecuredConnectionUserRecord> AuthRepository::LoadSecuredConnectionUsers(
    const std::optional<std::string>& search,
    bool include_revoked) const {
  std::string sql =
      "SELECT id, name, public_key, fingerprint, search_text, last_authorized_at, "
      "created_at, updated_at, revoked_at "
      "FROM secured_connection_users WHERE 1 = 1";
  if (!include_revoked) {
    sql += " AND revoked_at = ''";
  }
  if (search.has_value() && !search->empty()) {
    sql +=
        " AND lower(id || ' ' || name || ' ' || public_key || ' ' || fingerprint || "
        "' ' || search_text || ' ' || last_authorized_at || ' ' || created_at || "
        "' ' || updated_at) LIKE lower(?1)";
  }
  sql += " ORDER BY name ASC, created_at ASC, id ASC;";
  Statement statement(db_, sql);
  if (search.has_value() && !search->empty()) {
    statement.BindText(1, "%" + *search + "%");
  }
  std::vector<SecuredConnectionUserRecord> users;
  while (statement.StepRow()) {
    users.push_back(ReadSecuredConnectionUser(statement.raw()));
  }
  return users;
}

bool AuthRepository::RevokeSecuredConnectionUser(
    const std::string& user_id,
    const std::string& revoked_at) {
  Statement statement(
      db_,
      "UPDATE secured_connection_users "
      "SET revoked_at = ?2, updated_at = CURRENT_TIMESTAMP "
      "WHERE id = ?1 AND revoked_at = '';");
  statement.BindText(1, user_id);
  statement.BindText(2, revoked_at);
  statement.StepDone();
  return sqlite3_changes(db_) > 0;
}

void AuthRepository::TouchSecuredConnectionUser(
    const std::string& user_id,
    const std::string& last_authorized_at) {
  Statement statement(
      db_,
      "UPDATE secured_connection_users "
      "SET last_authorized_at = ?2, updated_at = CURRENT_TIMESTAMP "
      "WHERE id = ?1;");
  statement.BindText(1, user_id);
  statement.BindText(2, last_authorized_at);
  statement.StepDone();
}

void AuthRepository::InsertSecuredConnectionSession(
    const SecuredConnectionSessionRecord& session) {
  Statement statement(
      db_,
      "INSERT INTO secured_connection_sessions("
      "token, user_id, plane_name, expires_at, last_used_at"
      ") VALUES (?1, ?2, ?3, ?4, ?5);");
  statement.BindText(1, session.token);
  statement.BindText(2, session.user_id);
  statement.BindText(3, session.plane_name);
  statement.BindText(4, session.expires_at);
  statement.BindText(5, session.last_used_at);
  statement.StepDone();
}

std::optional<SecuredConnectionSessionRecord>
AuthRepository::LoadActiveSecuredConnectionSession(
    const std::string& token,
    const std::string& plane_name) const {
  Statement statement(
      db_,
      "SELECT token, user_id, plane_name, expires_at, created_at, revoked_at, last_used_at "
      "FROM secured_connection_sessions "
      "WHERE token = ?1 AND plane_name = ?2 AND revoked_at = '' "
      "AND expires_at >= CURRENT_TIMESTAMP;");
  statement.BindText(1, token);
  statement.BindText(2, plane_name);
  if (!statement.StepRow()) {
    return std::nullopt;
  }
  return ReadSecuredConnectionSession(statement.raw());
}

bool AuthRepository::TouchSecuredConnectionSession(
    const std::string& token,
    const std::string& last_used_at) {
  Statement statement(
      db_,
      "UPDATE secured_connection_sessions SET last_used_at = ?2 WHERE token = ?1;");
  statement.BindText(1, token);
  statement.BindText(2, last_used_at);
  statement.StepDone();
  return sqlite3_changes(db_) > 0;
}

void AuthRepository::InsertSecuredConnectionAuthLog(
    const SecuredConnectionAuthLogRecord& log) {
  Statement statement(
      db_,
      "INSERT INTO secured_connection_auth_log("
      "plane_name, user_id, user_name, fingerprint, event_type, outcome, "
      "remote_addr, message, payload_json"
      ") VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9);");
  statement.BindText(1, log.plane_name);
  statement.BindText(2, log.user_id);
  statement.BindText(3, log.user_name);
  statement.BindText(4, log.fingerprint);
  statement.BindText(5, log.event_type);
  statement.BindText(6, log.outcome);
  statement.BindText(7, log.remote_addr);
  statement.BindText(8, log.message);
  statement.BindText(9, log.payload_json.empty() ? std::string("{}") : log.payload_json);
  statement.StepDone();
}

std::vector<SecuredConnectionAuthLogRecord>
AuthRepository::LoadSecuredConnectionAuthLog(
    const std::optional<std::string>& plane_name,
    const std::optional<std::string>& user_id,
    int limit) const {
  std::string sql =
      "SELECT id, plane_name, user_id, user_name, fingerprint, event_type, outcome, "
      "remote_addr, message, created_at, payload_json "
      "FROM secured_connection_auth_log WHERE 1 = 1";
  int next_index = 1;
  if (plane_name.has_value() && !plane_name->empty()) {
    sql += " AND plane_name = ?" + std::to_string(next_index++);
  }
  if (user_id.has_value() && !user_id->empty()) {
    sql += " AND user_id = ?" + std::to_string(next_index++);
  }
  sql += " ORDER BY created_at DESC, id DESC LIMIT ?" + std::to_string(next_index) + ";";
  Statement statement(db_, sql);
  next_index = 1;
  if (plane_name.has_value() && !plane_name->empty()) {
    statement.BindText(next_index++, *plane_name);
  }
  if (user_id.has_value() && !user_id->empty()) {
    statement.BindText(next_index++, *user_id);
  }
  statement.BindInt(next_index, limit <= 0 ? 100 : limit);
  std::vector<SecuredConnectionAuthLogRecord> logs;
  while (statement.StepRow()) {
    logs.push_back(ReadSecuredConnectionAuthLog(statement.raw()));
  }
  return logs;
}

UserRecord AuthRepository::ReadUser(sqlite3_stmt* statement) {
  return UserRecord{
      sqlite3_column_int(statement, 0),
      ToColumnText(statement, 1),
      ToColumnText(statement, 2),
      ToColumnText(statement, 3),
      ToColumnText(statement, 4),
      ToColumnText(statement, 5),
      ToColumnText(statement, 6),
  };
}

WebAuthnCredentialRecord AuthRepository::ReadWebAuthnCredential(
    sqlite3_stmt* statement) {
  return WebAuthnCredentialRecord{
      sqlite3_column_int(statement, 0),
      sqlite3_column_int(statement, 1),
      ToColumnText(statement, 2),
      ToColumnText(statement, 3),
      static_cast<std::uint32_t>(sqlite3_column_int(statement, 4)),
      ToColumnText(statement, 5),
      ToColumnText(statement, 6),
      ToColumnText(statement, 7),
      ToColumnText(statement, 8),
  };
}

RegistrationInviteRecord AuthRepository::ReadRegistrationInvite(
    sqlite3_stmt* statement) {
  return RegistrationInviteRecord{
      sqlite3_column_int(statement, 0),
      ToColumnText(statement, 1),
      sqlite3_column_int(statement, 2),
      ToColumnText(statement, 3),
      ToColumnText(statement, 4),
      ToOptionalColumnInt(statement, 5),
      ToColumnText(statement, 6),
      ToColumnText(statement, 7),
  };
}

UserSshKeyRecord AuthRepository::ReadUserSshKey(sqlite3_stmt* statement) {
  return UserSshKeyRecord{
      sqlite3_column_int(statement, 0),
      sqlite3_column_int(statement, 1),
      ToColumnText(statement, 2),
      ToColumnText(statement, 3),
      ToColumnText(statement, 4),
      ToColumnText(statement, 5),
      ToColumnText(statement, 6),
      ToColumnText(statement, 7),
  };
}

AuthSessionRecord AuthRepository::ReadAuthSession(sqlite3_stmt* statement) {
  return AuthSessionRecord{
      ToColumnText(statement, 0),
      sqlite3_column_int(statement, 1),
      ToColumnText(statement, 2),
      ToColumnText(statement, 3),
      ToColumnText(statement, 4),
      ToColumnText(statement, 5),
      ToColumnText(statement, 6),
      ToColumnText(statement, 7),
  };
}

SecuredConnectionUserRecord AuthRepository::ReadSecuredConnectionUser(
    sqlite3_stmt* statement) {
  return SecuredConnectionUserRecord{
      ToColumnText(statement, 0),
      ToColumnText(statement, 1),
      ToColumnText(statement, 2),
      ToColumnText(statement, 3),
      ToColumnText(statement, 4),
      ToColumnText(statement, 5),
      ToColumnText(statement, 6),
      ToColumnText(statement, 7),
      ToColumnText(statement, 8),
  };
}

SecuredConnectionSessionRecord AuthRepository::ReadSecuredConnectionSession(
    sqlite3_stmt* statement) {
  return SecuredConnectionSessionRecord{
      ToColumnText(statement, 0),
      ToColumnText(statement, 1),
      ToColumnText(statement, 2),
      ToColumnText(statement, 3),
      ToColumnText(statement, 4),
      ToColumnText(statement, 5),
      ToColumnText(statement, 6),
  };
}

SecuredConnectionAuthLogRecord AuthRepository::ReadSecuredConnectionAuthLog(
    sqlite3_stmt* statement) {
  return SecuredConnectionAuthLogRecord{
      sqlite3_column_int(statement, 0),
      ToColumnText(statement, 1),
      ToColumnText(statement, 2),
      ToColumnText(statement, 3),
      ToColumnText(statement, 4),
      ToColumnText(statement, 5),
      ToColumnText(statement, 6),
      ToColumnText(statement, 7),
      ToColumnText(statement, 8),
      ToColumnText(statement, 9),
      ToColumnText(statement, 10),
  };
}

}  // namespace naim
