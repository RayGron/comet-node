#include "knowledge/knowledge_store.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

void Expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::filesystem::path TempStorePath() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  auto root = std::filesystem::temp_directory_path() /
              ("naim-knowledge-store-test-" + std::to_string(stamp));
  std::filesystem::create_directories(root);
  return root / "store";
}

}  // namespace

int main() {
  const auto store_path = TempStorePath();
  naim::knowledge_runtime::KnowledgeStore store(store_path);
  store.Open();
  Expect(
      store.Status("kv-test").value("storage_engine", std::string{}) == "rocksdb",
      "canonical store should report rocksdb storage");

  const auto ingest = store.IngestSource(nlohmann::json{
      {"source_kind", "document"},
      {"source_ref", "doc://store-test"},
      {"content", "Knowledge store scheduled merge and Markdown export test."},
      {"scope_ids", nlohmann::json::array({"scope.default"})},
      {"metadata", nlohmann::json{{"title", "Store Test"}}},
  });
  Expect(ingest.value("status", std::string{}) == "accepted", "source ingest should accept");
  Expect(!ingest.value("chunks", nlohmann::json::array()).empty(), "source ingest should chunk content");
  Expect(!ingest.value("claims", nlohmann::json::array()).empty(), "source ingest should extract claims");

  const auto duplicate = store.IngestSource(nlohmann::json{
      {"source_kind", "document"},
      {"source_ref", "doc://store-test"},
      {"content", "Knowledge store scheduled merge and Markdown export test."},
      {"scope_ids", nlohmann::json::array({"scope.default"})},
  });
  Expect(duplicate.value("status", std::string{}) == "duplicate", "source ingest should deduplicate");

  const auto restricted = store.IngestSource(nlohmann::json{
      {"source_kind", "document"},
      {"source_ref", "doc://restricted-store-test"},
      {"content", "restricted.example.internal should never leak into public export warnings."},
      {"scope_ids", nlohmann::json::array({"scope.private"})},
      {"metadata", nlohmann::json{{"title", "Restricted Store Test"}}},
  });
  Expect(restricted.value("status", std::string{}) == "accepted", "restricted source ingest should accept");

  const auto search = store.Search(nlohmann::json{
      {"query", "scheduled merge"},
      {"scope_id", "scope.default"},
  });
  Expect(!search.value("results", nlohmann::json::array()).empty(), "search should return result");

  store.WriteBlock(nlohmann::json{
      {"block_id", "lt-cypher-ai.market.assets"},
      {"knowledge_id", "lt-cypher-ai.market.assets"},
      {"title", "lt-cypher-ai market asset memory"},
      {"body", "BTC latest price and bitcoin market observations from LocalTrade and CoinGecko."},
      {"scope_ids", nlohmann::json::array({"scope.default"})},
  });
  const auto token_search = store.Search(nlohmann::json{
      {"query", "bitcoin market BTC LocalTrade"},
      {"scope_id", "scope.default"},
      {"limit", 5},
  });
  const auto token_results = token_search.value("results", nlohmann::json::array());
  Expect(!token_results.empty(), "token search should find deterministic market blocks");
  Expect(
      token_results.front().value("block_id", std::string{}) ==
          "lt-cypher-ai.market.assets",
      "token search should rank the matching deterministic market block first");

  const auto client_export = store.ClientSyncExport(nlohmann::json{
      {"scope_ids", nlohmann::json::array({"scope.default"})},
  });
  Expect(client_export.value("status", std::string{}) == "ok", "client sync export should succeed");
  Expect(!client_export.value("blocks", nlohmann::json::array()).empty(), "client sync export should include blocks");
  Expect(
      client_export.at("blocks").front().contains("_sync_hash"),
      "client sync export should include block sync hashes");

  const auto client_push = store.ClientSyncPush(nlohmann::json{
      {"writes",
       nlohmann::json::array({nlohmann::json{
           {"outbox_id", "outbox-1"},
           {"entity_type", "kv_block"},
           {"entity_id", "maglev.client.block"},
           {"operation", "upsert"},
           {"base_hash", ""},
           {"payload",
            nlohmann::json{
                {"block_id", "maglev.client.block"},
                {"knowledge_id", "maglev.client.block"},
                {"title", "Maglev Client Block"},
                {"body", "Client-owned Knowledge Vault write."},
                {"scope_ids", nlohmann::json::array({"scope.default"})},
            }},
       }})},
  });
  Expect(
      client_push.at("accepted_outbox_ids") == nlohmann::json::array({"outbox-1"}),
      "client sync push should accept non-conflicting block writes");
  Expect(
      store.ReadBlock("maglev.client.block").value("block", nlohmann::json::object())
          .value("title", std::string{}) == "Maglev Client Block",
      "client sync push should persist accepted block");

  const auto conflicting_push = store.ClientSyncPush(nlohmann::json{
      {"writes",
       nlohmann::json::array({nlohmann::json{
           {"outbox_id", "outbox-2"},
           {"entity_type", "kv_block"},
           {"entity_id", "maglev.client.block"},
           {"operation", "upsert"},
           {"base_hash", "sha256:not-current"},
           {"payload",
            nlohmann::json{
                {"block_id", "maglev.client.block"},
                {"knowledge_id", "maglev.client.block"},
                {"title", "Conflicting Maglev Client Block"},
                {"body", "This should not overwrite remote state."},
                {"scope_ids", nlohmann::json::array({"scope.default"})},
            }},
       }})},
  });
  Expect(
      !conflicting_push.value("conflicts", nlohmann::json::array()).empty(),
      "client sync push should report stale-base conflicts");
  Expect(
      store.ReadBlock("maglev.client.block").value("block", nlohmann::json::object())
          .value("title", std::string{}) == "Maglev Client Block",
      "client sync conflict should not overwrite remote block");

  const auto redacted = store.Search(nlohmann::json{
      {"query", "scheduled merge"},
      {"scope_id", "scope.other"},
  });
  Expect(
      redacted.at("results").front().at("redaction").value("reason", std::string{}) ==
          "out_of_scope",
      "search should redact out-of-scope result");

  const auto capsule = store.BuildCapsule(nlohmann::json{
      {"plane_id", "plane-test"},
      {"capsule_id", "cap-test"},
      {"included", nlohmann::json::array({ingest.value("source_block_id", std::string{})})},
  });
  Expect(capsule.value("capsule_id", std::string{}) == "cap-test", "capsule should build");
  Expect(
      store.ReadCapsule("cap-test").value("status", std::string{}) == "valid",
      "capsule should validate");
  Expect(
      capsule.at("manifest").value("storage_engine", std::string{}) == "rocksdb",
      "capsule manifest should advertise rocksdb-compatible storage");

  store.CatalogUpsert(nlohmann::json{
      {"object_id", ingest.value("source_block_id", std::string{})},
      {"shard_id", "storage-hpc1"},
      {"scope_ids", nlohmann::json::array({"scope.default"})},
      {"hints", nlohmann::json::array({"scheduled merge", "knowledge store"})},
      {"boundary_edges",
       nlohmann::json::array({nlohmann::json{
           {"edge_id", "edge-cross-shard"},
           {"from_object_id", ingest.value("source_block_id", std::string{})},
           {"to_object_id", "remote-block"},
           {"from_shard_id", "storage-hpc1"},
           {"to_shard_id", "storage-main"},
           {"type", "references"},
           {"scope_ids", nlohmann::json::array({"scope.default"})},
       }})},
      {"shard_health", nlohmann::json{{"status", "healthy"}, {"detail", nlohmann::json::object()}}},
  });
  const auto route = store.QueryRoute(nlohmann::json{
      {"query", "scheduled merge"},
      {"scope_ids", nlohmann::json::array({"scope.default"})},
  });
  Expect(!route.value("shard_requests", nlohmann::json::array()).empty(), "query route should plan shard fetches");

  store.ScheduleReplicaMerge(nlohmann::json{
      {"plane_id", "plane-test"},
      {"capsule_id", "cap-test"},
      {"cadence", "daily"},
  });
  store.WriteOverlay(nlohmann::json{
      {"plane_id", "plane-test"},
      {"capsule_id", "cap-test"},
      {"change_type", "claim_add"},
      {"base_versions", nlohmann::json::object()},
      {"proposed_blocks",
       nlohmann::json::array({nlohmann::json{
           {"knowledge_id", "knowledge.store-test"},
           {"title", "Merged Store Claim"},
           {"body", "Merged through scheduled replica reconciliation."},
           {"scope_ids", nlohmann::json::array({"scope.default"})},
       }})},
  });
  const auto due = store.RunScheduledReplicaMerges(nlohmann::json{
      {"plane_id", "plane-test"},
      {"force", true},
  });
  Expect(due.at("jobs").front().value("status", std::string{}) == "completed", "scheduled merge should complete");
  const auto daily = store.ReconcileDailyReplicaSchedules(nlohmann::json{{"plane_id", "plane-test"}});
  Expect(daily.value("status", std::string{}) == "completed", "daily reconcile should complete");

  store.WriteOverlay(nlohmann::json{
      {"plane_id", "protected-plane"},
      {"capsule_id", "private-cap"},
      {"protected_plane", true},
      {"change_type", "claim_add"},
      {"base_versions", nlohmann::json::object()},
      {"proposed_blocks",
       nlohmann::json::array({nlohmann::json{
           {"knowledge_id", "knowledge.protected-leak-check"},
           {"title", "Protected Leak Check"},
           {"body", "Protected plane knowledge must not enter the common vault."},
           {"scope_ids", nlohmann::json::array({"scope.default"})},
       }})},
  });
  const auto protected_merge = store.TriggerReplicaMerge(nlohmann::json{
      {"plane_id", "protected-plane"},
      {"capsule_id", "private-cap"},
  });
  Expect(
      protected_merge.value("accepted", 0) == 0,
      "protected overlays should not merge into canonical knowledge");
  Expect(
      protected_merge.value("rejected", 0) == 1,
      "protected overlays should be counted as rejected by direct merge trigger");
  const auto protected_search = store.Search(nlohmann::json{
      {"query", "Protected Leak Check"},
      {"scope_id", "scope.default"},
  });
  Expect(
      protected_search.dump().find("knowledge.protected-leak-check") == std::string::npos,
      "protected overlay content should not be searchable in the common vault");

  store.WriteOverlay(nlohmann::json{
      {"plane_id", "plane-test"},
      {"capsule_id", "cap-test"},
      {"change_type", "claim_add"},
      {"confidence", 0.1},
      {"proposed_blocks", nlohmann::json::array()},
  });
  store.TriggerReplicaMerge(nlohmann::json{{"plane_id", "plane-test"}, {"capsule_id", "cap-test"}});
  const auto reviews = store.ListReviewItems(nlohmann::json{{"status", "pending"}});
  Expect(!reviews.value("items", nlohmann::json::array()).empty(), "low confidence overlay should enter review");

  const auto context = store.Context(nlohmann::json{
      {"query", "scheduled replica"},
      {"scope_id", "scope.default"},
      {"request_id", "req-store-test"},
      {"token_budget", 1200},
      {"plane_id", "plane-test"},
      {"capsule_id", "cap-test"},
  });
  Expect(!context.value("context", nlohmann::json::array()).empty(), "context should return bundle");

  const std::string source_block_id = ingest.value("source_block_id", std::string{});
  const auto linked = store.WriteBlock(nlohmann::json{
      {"block_id", "delete-linked-block"},
      {"knowledge_id", "knowledge.delete-linked"},
      {"title", "Delete Linked Block"},
      {"body", "A linked block used to verify relation cleanup."},
      {"scope_ids", nlohmann::json::array({"scope.default"})},
  });
  (void)linked;
  const auto relation = store.WriteRelation(nlohmann::json{
      {"relation_id", "rel-delete-test"},
      {"from_block_id", source_block_id},
      {"to_block_id", "delete-linked-block"},
      {"type", "related"},
  });
  Expect(relation.at("relation").value("relation_id", std::string{}) == "rel-delete-test", "relation should be written");
  Expect(
      !store.Neighbors(source_block_id).value("neighbors", nlohmann::json::array()).empty(),
      "relation should be visible before delete");
  const auto relation_delete = store.DeleteRelation("rel-delete-test", nlohmann::json{{"reason", "test relation cleanup"}});
  Expect(relation_delete.value("status", std::string{}) == "deleted", "relation delete should succeed");
  Expect(
      store.Neighbors(source_block_id).value("neighbors", nlohmann::json::array()).empty(),
      "relation should be hidden after delete");

  const auto source_delete = store.DeleteSource(source_block_id, nlohmann::json{{"reason", "test source cleanup"}});
  Expect(source_delete.value("status", std::string{}) == "deleted", "source delete should succeed by block id");
  const auto deleted_search = store.Search(nlohmann::json{
      {"query", "scheduled merge"},
      {"scope_id", "scope.default"},
  });
  Expect(
      deleted_search.dump().find(source_block_id) == std::string::npos,
      "deleted source block should not be searchable");
  const auto deleted_graph = store.GraphNeighborhood(nlohmann::json{{"center_id", source_block_id}});
  Expect(
      deleted_graph.value("nodes", nlohmann::json::array()).empty(),
      "deleted source block should not appear in graph");
  const auto cleanup_plan = store.Cleanup(nlohmann::json{{"apply", false}});
  Expect(cleanup_plan.value("status", std::string{}) == "planned", "cleanup dry-run should plan");
  const auto cleanup = store.Cleanup(nlohmann::json{{"apply", true}});
  Expect(cleanup.value("status", std::string{}) == "completed", "cleanup should complete");
  Expect(store.ReadBlock(source_block_id).contains("error"), "cleanup should physically purge deleted block");

  const auto repair = store.RunRepair(nlohmann::json{{"apply", true}, {"full_rebuild", true}});
  Expect(repair.contains("report_id"), "repair should persist a report");

  const auto markdown = store.MarkdownExport(nlohmann::json{
      {"scope_ids", nlohmann::json::array({"scope.default"})},
  });
  Expect(!markdown.value("files", nlohmann::json::array()).empty(), "markdown export should produce files");
  const std::string markdown_text = markdown.dump();
  Expect(
      markdown_text.find(restricted.value("source_block_id", std::string{})) == std::string::npos,
      "markdown export warnings should not leak restricted block ids");
  Expect(
      markdown_text.find("restricted.example.internal") == std::string::npos,
      "markdown export should not leak restricted content");
  Expect(
      markdown.at("files").front().value("content", std::string{}).find("related:") != std::string::npos,
      "markdown export should include Obsidian-compatible frontmatter");

  const auto import = store.MarkdownImport(nlohmann::json{
      {"plane_id", "plane-test"},
      {"capsule_id", "cap-test"},
      {"files",
       nlohmann::json::array({nlohmann::json{
           {"path", "Imported.md"},
           {"content", "# Imported\n\nImported proposal body."},
           {"scope_ids", nlohmann::json::array({"scope.default"})},
       }})},
  });
  Expect(
      !import.value("accepted_for_review", nlohmann::json::array()).empty(),
      "markdown import should create proposals");

  const auto protected_store_path = TempStorePath();
  naim::knowledge_runtime::KnowledgeStore protected_store(protected_store_path, true);
  protected_store.Open();
  protected_store.WriteOverlay(nlohmann::json{
      {"plane_id", "env-protected-plane"},
      {"capsule_id", "env-private-cap"},
      {"change_type", "claim_add"},
      {"base_versions", nlohmann::json::object()},
      {"proposed_blocks",
       nlohmann::json::array({nlohmann::json{
           {"knowledge_id", "knowledge.env-protected"},
           {"title", "Env Protected Claim"},
           {"body", "Runtime protected plane data stays isolated."},
           {"scope_ids", nlohmann::json::array({"scope.default"})},
       }})},
  });
  const auto env_protected_merge = protected_store.TriggerReplicaMerge(nlohmann::json{
      {"plane_id", "env-protected-plane"},
      {"capsule_id", "env-private-cap"},
  });
  Expect(
      env_protected_merge.value("status", std::string{}) == "skipped",
      "protected runtime should skip replica merge into the common vault");

  std::cout << "ok: knowledge-store-integration\n";
  return 0;
}
