#include "comet/state/registered_host_repository.h"

#include <string>

#include "comet/state/sqlite_statement.h"
#include "comet/state/sqlite_store_support.h"

namespace comet {

namespace {

using Statement = SqliteStatement;
using sqlite_store_support::ToColumnText;

RegisteredHostRecord RegisteredHostFromStatement(sqlite3_stmt* statement) {
  RegisteredHostRecord host;
  host.node_name = ToColumnText(statement, 0);
  host.advertised_address = ToColumnText(statement, 1);
  host.public_key_base64 = ToColumnText(statement, 2);
  host.controller_public_key_fingerprint = ToColumnText(statement, 3);
  host.transport_mode = ToColumnText(statement, 4);
  host.execution_mode = ToColumnText(statement, 5);
  host.registration_state = ToColumnText(statement, 6);
  host.onboarding_key_hash = ToColumnText(statement, 7);
  host.onboarding_state = ToColumnText(statement, 8);
  host.derived_role = ToColumnText(statement, 9);
  host.role_reason = ToColumnText(statement, 10);
  host.last_inventory_scan_at = ToColumnText(statement, 11);
  host.session_state = ToColumnText(statement, 12);
  host.session_token = ToColumnText(statement, 13);
  host.session_expires_at = ToColumnText(statement, 14);
  host.session_host_sequence = sqlite3_column_int64(statement, 15);
  host.session_controller_sequence = sqlite3_column_int64(statement, 16);
  host.capabilities_json = ToColumnText(statement, 17);
  host.status_message = ToColumnText(statement, 18);
  host.last_session_at = ToColumnText(statement, 19);
  host.last_heartbeat_at = ToColumnText(statement, 20);
  host.created_at = ToColumnText(statement, 21);
  host.updated_at = ToColumnText(statement, 22);
  return host;
}

}  // namespace

RegisteredHostRepository::RegisteredHostRepository(sqlite3* db) : db_(db) {}

void RegisteredHostRepository::UpsertRegisteredHost(const RegisteredHostRecord& host) {
  Statement statement(
      db_,
      "INSERT INTO registered_hosts("
      " node_name,"
      " advertised_address,"
      " public_key_base64,"
      " controller_public_key_fingerprint,"
      " transport_mode,"
      " execution_mode,"
      " registration_state,"
      " onboarding_key_hash,"
      " onboarding_state,"
      " derived_role,"
      " role_reason,"
      " last_inventory_scan_at,"
      " session_state,"
      " session_token,"
      " session_expires_at,"
      " session_host_sequence,"
      " session_controller_sequence,"
      " capabilities_json,"
      " status_message,"
      " last_session_at,"
      " last_heartbeat_at,"
      " updated_at"
      ") VALUES ("
      " ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, CURRENT_TIMESTAMP"
      ")"
      " ON CONFLICT(node_name) DO UPDATE SET"
      " advertised_address = excluded.advertised_address,"
      " public_key_base64 = excluded.public_key_base64,"
      " controller_public_key_fingerprint = excluded.controller_public_key_fingerprint,"
      " transport_mode = excluded.transport_mode,"
      " execution_mode = excluded.execution_mode,"
      " registration_state = excluded.registration_state,"
      " onboarding_key_hash = excluded.onboarding_key_hash,"
      " onboarding_state = excluded.onboarding_state,"
      " derived_role = excluded.derived_role,"
      " role_reason = excluded.role_reason,"
      " last_inventory_scan_at = excluded.last_inventory_scan_at,"
      " session_state = excluded.session_state,"
      " session_token = excluded.session_token,"
      " session_expires_at = excluded.session_expires_at,"
      " session_host_sequence = excluded.session_host_sequence,"
      " session_controller_sequence = excluded.session_controller_sequence,"
      " capabilities_json = excluded.capabilities_json,"
      " status_message = excluded.status_message,"
      " last_session_at = excluded.last_session_at,"
      " last_heartbeat_at = excluded.last_heartbeat_at,"
      " updated_at = CURRENT_TIMESTAMP;");
  statement.BindText(1, host.node_name);
  statement.BindText(2, host.advertised_address);
  statement.BindText(3, host.public_key_base64);
  statement.BindText(4, host.controller_public_key_fingerprint);
  statement.BindText(5, host.transport_mode);
  statement.BindText(6, host.execution_mode);
  statement.BindText(7, host.registration_state);
  statement.BindText(8, host.onboarding_key_hash);
  statement.BindText(9, host.onboarding_state);
  statement.BindText(10, host.derived_role);
  statement.BindText(11, host.role_reason);
  statement.BindText(12, host.last_inventory_scan_at);
  statement.BindText(13, host.session_state);
  statement.BindText(14, host.session_token);
  statement.BindText(15, host.session_expires_at);
  statement.BindInt(16, static_cast<int>(host.session_host_sequence));
  statement.BindInt(17, static_cast<int>(host.session_controller_sequence));
  statement.BindText(18, host.capabilities_json);
  statement.BindText(19, host.status_message);
  statement.BindText(20, host.last_session_at);
  statement.BindText(21, host.last_heartbeat_at);
  statement.StepDone();
}

std::optional<RegisteredHostRecord> RegisteredHostRepository::LoadRegisteredHost(
    const std::string& node_name) const {
  Statement statement(
      db_,
      "SELECT node_name,"
      " advertised_address,"
      " public_key_base64,"
      " controller_public_key_fingerprint,"
      " transport_mode,"
      " execution_mode,"
      " registration_state,"
      " onboarding_key_hash,"
      " onboarding_state,"
      " derived_role,"
      " role_reason,"
      " last_inventory_scan_at,"
      " session_state,"
      " session_token,"
      " session_expires_at,"
      " session_host_sequence,"
      " session_controller_sequence,"
      " capabilities_json,"
      " status_message,"
      " last_session_at,"
      " last_heartbeat_at,"
      " created_at,"
      " updated_at"
      " FROM registered_hosts WHERE node_name = ?1;");
  statement.BindText(1, node_name);
  if (!statement.StepRow()) {
    return std::nullopt;
  }
  return RegisteredHostFromStatement(statement.raw());
}

std::vector<RegisteredHostRecord> RegisteredHostRepository::LoadRegisteredHosts(
    const std::optional<std::string>& node_name) const {
  std::string sql =
      "SELECT node_name,"
      " advertised_address,"
      " public_key_base64,"
      " controller_public_key_fingerprint,"
      " transport_mode,"
      " execution_mode,"
      " registration_state,"
      " onboarding_key_hash,"
      " onboarding_state,"
      " derived_role,"
      " role_reason,"
      " last_inventory_scan_at,"
      " session_state,"
      " session_token,"
      " session_expires_at,"
      " session_host_sequence,"
      " session_controller_sequence,"
      " capabilities_json,"
      " status_message,"
      " last_session_at,"
      " last_heartbeat_at,"
      " created_at,"
      " updated_at"
      " FROM registered_hosts";
  if (node_name.has_value()) {
    sql += " WHERE node_name = ?1";
  }
  sql += " ORDER BY node_name ASC;";
  Statement statement(db_, sql);
  if (node_name.has_value()) {
    statement.BindText(1, *node_name);
  }

  std::vector<RegisteredHostRecord> hosts;
  while (statement.StepRow()) {
    hosts.push_back(RegisteredHostFromStatement(statement.raw()));
  }
  return hosts;
}

}  // namespace comet
