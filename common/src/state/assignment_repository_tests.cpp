#include "naim/state/sqlite_store.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#include <sqlite3.h>

namespace {

void Expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

naim::HostAssignment MakeAssignment(
    const std::string& node_name,
    const std::string& plane_name,
    const std::string& assignment_type = "runtime-http-proxy") {
  naim::HostAssignment assignment;
  assignment.node_name = node_name;
  assignment.plane_name = plane_name;
  assignment.desired_generation = 0;
  assignment.max_attempts = 3;
  assignment.assignment_type = assignment_type;
  assignment.desired_state_json = "{}";
  assignment.status = naim::HostAssignmentStatus::Pending;
  assignment.status_message = "queued by test";
  return assignment;
}

std::filesystem::path TempDbPath(const std::string& name) {
  const auto suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         (name + "-" + std::to_string(suffix) + ".sqlite");
}

void ExecSql(const std::filesystem::path& db_path, const std::string& sql) {
  sqlite3* db = nullptr;
  if (sqlite3_open(db_path.string().c_str(), &db) != SQLITE_OK) {
    const std::string message = db == nullptr ? "unknown" : sqlite3_errmsg(db);
    if (db != nullptr) {
      sqlite3_close(db);
    }
    throw std::runtime_error("failed to open test sqlite db: " + message);
  }
  char* error_message = nullptr;
  const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error_message);
  if (rc != SQLITE_OK) {
    const std::string message =
        error_message == nullptr ? "unknown" : error_message;
    sqlite3_free(error_message);
    sqlite3_close(db);
    throw std::runtime_error("test sqlite exec failed: " + message);
  }
  sqlite3_close(db);
}

void RuntimeProxyInteractionClaimsAheadOfWebGatewayStatus() {
  const auto db_path = TempDbPath("naim-assignment-priority-test");
  naim::ControllerStore store(db_path.string());
  store.Initialize();
  store.EnqueueHostAssignments(
      {MakeAssignment(
           "hpc1",
           "runtime-http-proxy:webgateway:maglev-service:webgateway-old"),
       MakeAssignment("hpc1", "runtime-http-proxy:maglev-service:chat-new")},
      "test supersede");

  const auto claimed = store.ClaimNextHostAssignment("hpc1");
  Expect(claimed.has_value(), "expected a claimed assignment");
  Expect(
      claimed->plane_name == "runtime-http-proxy:maglev-service:chat-new",
      "interaction proxy should claim before webgateway status proxy");
  std::filesystem::remove(db_path);
  std::cout << "ok: runtime-proxy-interaction-priority\n";
}

void ExpiredRuntimeProxyPendingAssignmentsAreFailedBeforeClaim() {
  const auto db_path = TempDbPath("naim-assignment-expiry-test");
  naim::ControllerStore store(db_path.string());
  store.Initialize();
  store.EnqueueHostAssignments(
      {MakeAssignment("hpc1", "runtime-http-proxy:webgateway:plane:old"),
       MakeAssignment("hpc1", "regular-plane", "desired-state-apply")},
      "test supersede");
  ExecSql(
      db_path,
      "UPDATE host_assignments "
      "SET updated_at = datetime('now', '-120 seconds') "
      "WHERE assignment_type = 'runtime-http-proxy';");

  const auto claimed = store.ClaimNextHostAssignment("hpc1");
  Expect(claimed.has_value(), "expected a non-expired assignment");
  Expect(
      claimed->assignment_type == "desired-state-apply",
      "expired runtime proxy should not be claimed ahead of regular work");
  const auto proxy_assignments = store.LoadHostAssignments(
      std::optional<std::string>("hpc1"),
      std::optional<naim::HostAssignmentStatus>(naim::HostAssignmentStatus::Failed),
      std::optional<std::string>("runtime-http-proxy:webgateway:plane:old"));
  Expect(
      proxy_assignments.size() == 1,
      "expired runtime proxy should be marked failed");
  std::filesystem::remove(db_path);
  std::cout << "ok: runtime-proxy-expiry-before-claim\n";
}

void ExpiredClaimedAssignmentsAreRequeuedBeforeClaim() {
  const auto db_path = TempDbPath("naim-assignment-claimed-lease-test");
  naim::ControllerStore store(db_path.string());
  store.Initialize();
  store.EnqueueHostAssignments(
      {MakeAssignment("hpc1", "plane-a", "apply-node-state")},
      "test supersede");

  const auto first_claim = store.ClaimNextHostAssignment("hpc1");
  Expect(first_claim.has_value(), "expected first claim");
  Expect(first_claim->attempt_count == 1, "first claim should increment attempt count");
  ExecSql(
      db_path,
      "UPDATE host_assignments "
      "SET updated_at = datetime('now', '-120 seconds') "
      "WHERE id = " + std::to_string(first_claim->id) + ";");

  const auto retry_claim = store.ClaimNextHostAssignment("hpc1");
  Expect(retry_claim.has_value(), "expired claimed assignment should be requeued");
  Expect(
      retry_claim->id == first_claim->id,
      "retry should claim the same expired assignment");
  Expect(retry_claim->attempt_count == 2, "retry claim should increment attempt count");
  std::filesystem::remove(db_path);
  std::cout << "ok: claimed-assignment-lease-requeue\n";
}

void ExpiredClaimedAssignmentsFailAfterExhaustedAttempts() {
  const auto db_path = TempDbPath("naim-assignment-claimed-exhausted-test");
  naim::ControllerStore store(db_path.string());
  store.Initialize();
  auto assignment = MakeAssignment("hpc1", "plane-a", "apply-node-state");
  assignment.max_attempts = 1;
  store.EnqueueHostAssignments({assignment}, "test supersede");

  const auto first_claim = store.ClaimNextHostAssignment("hpc1");
  Expect(first_claim.has_value(), "expected first claim");
  ExecSql(
      db_path,
      "UPDATE host_assignments "
      "SET updated_at = datetime('now', '-120 seconds') "
      "WHERE id = " + std::to_string(first_claim->id) + ";");

  const auto retry_claim = store.ClaimNextHostAssignment("hpc1");
  Expect(!retry_claim.has_value(), "exhausted expired claim should not be requeued");
  const auto failed = store.LoadHostAssignments(
      std::optional<std::string>("hpc1"),
      std::optional<naim::HostAssignmentStatus>(naim::HostAssignmentStatus::Failed),
      std::optional<std::string>("plane-a"));
  Expect(failed.size() == 1, "exhausted expired claim should be marked failed");
  std::filesystem::remove(db_path);
  std::cout << "ok: claimed-assignment-lease-exhaustion\n";
}

}  // namespace

int main() {
  try {
    RuntimeProxyInteractionClaimsAheadOfWebGatewayStatus();
    ExpiredRuntimeProxyPendingAssignmentsAreFailedBeforeClaim();
    ExpiredClaimedAssignmentsAreRequeuedBeforeClaim();
    ExpiredClaimedAssignmentsFailAfterExhaustedAttempts();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "assignment_repository_tests failed: " << ex.what() << '\n';
    return 1;
  }
}
