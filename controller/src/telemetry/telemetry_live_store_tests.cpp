#include "telemetry/telemetry_live_store.h"

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

std::uint64_t CurrentMillis() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

naim::HostTelemetryFrame MakeFrame(
    const std::string& node_name,
    const std::string& plane_name,
    std::uint64_t sequence) {
  naim::HostTelemetryFrame frame;
  frame.node_name = node_name;
  frame.node_id = node_name;
  frame.plane_name = plane_name;
  frame.plane_id = plane_name;
  frame.schema_version = "host.telemetry.v2";
  frame.source = "hostd";
  frame.sequence = sequence;
  frame.sampled_at = "2026-05-04 12:00:00";
  frame.collected_at = frame.sampled_at;
  frame.expires_at = "2026-05-04 12:00:10";
  frame.monotonic_ms = sequence;
  frame.monotonic_timestamp_ms = sequence;
  frame.lane = "fast";
  frame.collector_duration_ms = 2;
  frame.publish_duration_ms = 3;
  frame.publisher_queue_delay_ms = 4;
  frame.telemetry_bus_depth = 1;
  frame.telemetry_dropped_frames = 0;
  frame.adaptive_interval_ms = 2000;
  frame.adaptive_reason = "test";
  frame.plane_instance_count = 2;
  frame.plane_ready_instance_count = 1;
  frame.plane_not_ready_instance_count = 1;
  frame.plane_runtime_health = "changing";
  frame.cpu.source = "procfs";
  frame.cpu.total_memory_bytes = 100;
  frame.cpu.used_memory_bytes = 50;
  frame.gpu.source = "nvml";
  frame.gpu.devices.push_back(naim::GpuDeviceTelemetry{
      "0",
      100,
      25,
      75,
      10,
      45,
      true,
      {},
  });
  return frame;
}

void TestUpsertAndSnapshot() {
  auto& store = naim::controller::TelemetryLiveStore::Instance();
  Expect(store.Upsert(MakeFrame("node-a", "plane-a", 100)), "first frame should update");
  Expect(!store.Upsert(MakeFrame("node-a", "plane-a", 99)), "older frame should be ignored");
  Expect(store.Upsert(MakeFrame("node-a", "plane-a", 101)), "newer frame should update");

  const auto snapshot = store.BuildSnapshot(std::nullopt, 10);
  Expect(snapshot.at("latest_sequence").get<std::uint64_t>() >= 101, "latest sequence should advance");
  Expect(!snapshot.at("nodes").empty(), "snapshot should include latest nodes");
  std::cout << "ok: telemetry-live-store-upsert-snapshot\n";
}

void TestPlaneFilter() {
  auto& store = naim::controller::TelemetryLiveStore::Instance();
  Expect(store.Upsert(MakeFrame("node-b", "plane-b", 200)), "plane-b frame should update");

  const auto plane_a = store.BuildSnapshot(std::string("plane-a"), 0);
  const auto plane_b = store.BuildSnapshot(std::string("plane-b"), 0);
  Expect(plane_a.at("nodes").size() >= 1, "plane-a snapshot should include plane-a node");
  Expect(plane_b.at("nodes").size() == 1, "plane-b snapshot should include only plane-b node");
  Expect(
      plane_b.at("nodes").at(0).at("node_name").get<std::string>() == "node-b",
      "plane-b snapshot should expose node-b");
  std::cout << "ok: telemetry-live-store-plane-filter\n";
}

void TestCoherenceFieldsAndStaleProjection() {
  auto& store = naim::controller::TelemetryLiveStore::Instance();
  auto fresh = MakeFrame("node-coherent", "plane-coherent", CurrentMillis());
  fresh.ttl_ms = 60000;
  fresh.monotonic_ms = 1234;
  fresh.monotonic_timestamp_ms = 1234;
  fresh.lane = "full";
  fresh.degraded_reason = "cpu:procfs-warmup";
  fresh.disk.source = "hostd";
  fresh.disk.items.push_back(naim::DiskTelemetryRecord{
      "root",
      "plane-coherent",
      "node-coherent",
      "/tmp",
      "",
      "",
      "mounted",
      "healthy",
      "",
      100,
      25,
      75,
      1,
      2,
      3,
      4,
      0,
      0,
      0,
      0,
      0,
      0,
      false,
      true,
      true,
      true,
      {},
  });
  Expect(store.Upsert(fresh), "fresh coherent frame should update");

  const auto snapshot = store.BuildSnapshot(std::string("plane-coherent"), 0);
  Expect(snapshot.at("nodes").size() == 1, "coherent snapshot should include one node");
  const auto node = snapshot.at("nodes").at(0);
  Expect(
      snapshot.at("schema_version").get<std::string>() == "telemetry.snapshot.v2",
      "snapshot schema should be versioned");
  Expect(
      snapshot.at("transport").get<std::string>() == "sse-primary",
      "snapshot should declare streaming transport");
  Expect(snapshot.contains("planes"), "snapshot should include plane aggregates");
  Expect(snapshot.contains("downsampled_history"), "snapshot should include downsampled history");
  Expect(
      node.at("schema_version").get<std::string>() == "host.telemetry.v2",
      "frame schema should roundtrip");
  Expect(node.at("source").get<std::string>() == "hostd", "source should roundtrip");
  Expect(node.at("node_id").get<std::string>() == "node-coherent", "node_id should roundtrip");
  Expect(node.at("channel").get<std::string>() == "host.telemetry.v1", "channel should roundtrip");
  Expect(node.at("lane").get<std::string>() == "full", "lane should roundtrip");
  Expect(node.at("monotonic_ms").get<std::uint64_t>() == 1234, "monotonic_ms should roundtrip");
  Expect(
      node.at("monotonic_timestamp_ms").get<std::uint64_t>() == 1234,
      "monotonic timestamp alias should roundtrip");
  Expect(
      node.at("collector_duration_ms").get<std::uint64_t>() == 2,
      "collector duration should roundtrip");
  Expect(
      node.at("publish_duration_ms").get<std::uint64_t>() == 3,
      "publish duration should roundtrip");
  Expect(
      node.at("publisher_queue_delay_ms").get<std::uint64_t>() == 4,
      "queue delay should roundtrip");
  Expect(
      node.at("adaptive_reason").get<std::string>() == "test",
      "adaptive reason should roundtrip");
  Expect(
      node.at("plane_runtime_health").get<std::string>() == "changing",
      "plane runtime health should roundtrip");
  Expect(
      node.contains("controller_ingest_delay_ms"),
      "controller ingest delay should be projected");
  Expect(node.contains("latency_breakdown"), "latency breakdown should be projected");
  Expect(node.contains("telemetry_health"), "telemetry health should be projected");
  Expect(
      node.at("telemetry_health").at("status").get<std::string>() == "degraded",
      "degraded frame should project degraded health");
  Expect(
      node.at("degraded_reason").get<std::string>() == "cpu:procfs-warmup",
      "degraded reason should roundtrip");
  Expect(!node.at("stale").get<bool>(), "fresh frame should not be stale");
  Expect(node.at("disk").at("items").size() == 1, "full frame should include disk telemetry");

  auto stale = MakeFrame("node-stale", "plane-stale", 1);
  stale.ttl_ms = 1;
  Expect(store.Upsert(stale), "stale frame should update");
  const auto stale_snapshot = store.BuildSnapshot(std::string("plane-stale"), 0);
  Expect(stale_snapshot.at("nodes").at(0).at("stale").get<bool>(), "old frame should be stale");
  std::cout << "ok: telemetry-live-store-coherence-stale\n";
}

void TestBackpressureProjection() {
  auto& store = naim::controller::TelemetryLiveStore::Instance();
  for (std::uint64_t index = 0; index < 320; ++index) {
    Expect(
        store.Upsert(MakeFrame("node-backpressure", "plane-backpressure", 1000 + index)),
        "backpressure frame should update");
  }
  const auto snapshot = store.BuildSnapshot(std::string("plane-backpressure"), 600);
  Expect(snapshot.at("telemetry_overloaded").get<bool>(), "snapshot should expose overload");
  Expect(
      snapshot.at("dropped_frames_total").get<std::uint64_t>() >= 20,
      "snapshot should count dropped frames");
  Expect(
      snapshot.at("history_capacity").get<std::size_t>() == 300,
      "snapshot should expose history capacity");
  Expect(
      snapshot.at("stream_batch_limit").get<std::size_t>() == 128,
      "snapshot should expose stream batch limit");
  Expect(
      snapshot.at("nodes").at(0).at("controller_dropped_frames_total").get<std::uint64_t>() >= 20,
      "node should expose dropped frames");
  const auto frames = store.LoadFramesAfter(1000, std::string("plane-backpressure"));
  Expect(frames.size() == 128, "stream load should be capped");
  Expect(frames.front().sequence > 1000, "stream cap should retain newer frames");
  const auto delta = store.LoadStreamDeltaAfter(1000, std::string("plane-backpressure"));
  Expect(delta.replay_required, "stream delta should request replay after truncation");
  Expect(delta.frames.size() == 128, "stream delta should be capped");
  Expect(delta.first_available_sequence > 0, "stream delta should expose first available sequence");
  std::cout << "ok: telemetry-live-store-backpressure\n";
}

void TestPlaneAggregatesAndDownsampling() {
  auto& store = naim::controller::TelemetryLiveStore::Instance();
  const auto now = CurrentMillis();
  Expect(store.Upsert(MakeFrame("node-plane-agg-a", "plane-agg", now - 1000)), "agg a");
  Expect(store.Upsert(MakeFrame("node-plane-agg-b", "plane-agg", now - 500)), "agg b");
  const auto snapshot = store.BuildSnapshot(std::string("plane-agg"), 120);
  Expect(!snapshot.at("planes").empty(), "snapshot should include plane aggregate");
  const auto plane = snapshot.at("planes").at(0);
  Expect(plane.at("plane_name").get<std::string>() == "plane-agg", "aggregate plane name");
  Expect(plane.at("node_count").get<std::uint64_t>() == 2, "aggregate should count nodes");
  Expect(
      plane.at("runtime").at("instance_count").get<std::uint64_t>() == 4,
      "aggregate should sum local runtime instances");
  Expect(!snapshot.at("downsampled_history").empty(), "downsampled history should be populated");
  std::cout << "ok: telemetry-live-store-plane-aggregates-downsampling\n";
}

void TestSqlitePersistenceReplayAndHealth() {
  auto& store = naim::controller::TelemetryLiveStore::Instance();
  const auto db_path =
      std::filesystem::temp_directory_path() /
      ("naim-telemetry-live-store-" + std::to_string(CurrentMillis()) + ".sqlite");
  store.ResetForTests();
  store.ConfigurePersistence(db_path.string(), 4);
  Expect(store.Upsert(MakeFrame("node-durable", "plane-durable", 4000)), "durable frame one");
  Expect(store.Upsert(MakeFrame("node-durable", "plane-durable", 4001)), "durable frame two");

  const auto before = store.BuildHealth(std::string("plane-durable"));
  Expect(
      before.at("persistence").at("enabled").get<bool>(),
      "sqlite persistence should be enabled");
  Expect(
      before.at("persistence").at("persisted_frames_total").get<std::uint64_t>() == 2,
      "sqlite persistence should count persisted frames");

  store.ResetForTests();
  store.ConfigurePersistence(db_path.string(), 4);
  const auto snapshot = store.BuildSnapshot(std::string("plane-durable"), 60);
  Expect(snapshot.at("nodes").size() == 1, "durable replay should restore latest node");
  Expect(
      snapshot.at("latest_sequence").get<std::uint64_t>() == 4001,
      "durable replay should restore latest sequence");
  Expect(
      snapshot.at("persistence").at("loaded_frames_total").get<std::uint64_t>() == 2,
      "durable replay should count loaded frames");
  const auto frames = store.LoadFramesAfter(0, std::string("plane-durable"));
  Expect(frames.size() == 2, "durable replay should restore stream history");

  std::filesystem::remove(db_path);
  store.ResetForTests();
  std::cout << "ok: telemetry-live-store-sqlite-persistence-health\n";
}

void TestConfigurableRetentionAndMetrics() {
  auto& store = naim::controller::TelemetryLiveStore::Instance();
  store.ResetForTests();
  naim::controller::TelemetryLiveStore::RetentionConfig retention;
  retention.hot_history_capacity = 3;
  retention.stream_batch_limit = 2;
  retention.durable_history_capacity = 5;
  retention.warm_bucket_ms = 1000;
  retention.cold_bucket_ms = 2000;
  naim::controller::TelemetryLiveStore::AlertThresholds thresholds;
  thresholds.queue_warning_ms = 1;
  store.ConfigureOperationalPolicy(retention, thresholds);
  for (std::uint64_t index = 0; index < 6; ++index) {
    Expect(
        store.Upsert(MakeFrame("node-config", "plane-config", 5000 + index)),
        "config frame should update");
  }
  const auto snapshot = store.BuildSnapshot(std::string("plane-config"), 30);
  Expect(snapshot.at("history_capacity").get<std::size_t>() == 3, "hot capacity should be configurable");
  Expect(snapshot.at("stream_batch_limit").get<std::size_t>() == 2, "stream cap should be configurable");
  Expect(
      snapshot.at("dropped_frames_total").get<std::uint64_t>() >= 3,
      "custom hot capacity should prune frames");
  const auto delta = store.LoadStreamDeltaAfter(0, std::string("plane-config"));
  Expect(delta.frames.size() == 2, "custom stream batch limit should cap deltas");
  Expect(delta.replay_required, "custom stream batch limit should require replay");
  Expect(!snapshot.at("alerts").empty(), "custom thresholds should produce alert candidates");
  std::cout << "ok: telemetry-live-store-configurable-retention\n";
}

void TestRecoveredTelemetryCountersDoNotDegradeHealth() {
  auto& store = naim::controller::TelemetryLiveStore::Instance();
  store.ResetForTests();
  naim::controller::TelemetryLiveStore::RetentionConfig retention;
  retention.hot_history_capacity = 3;
  retention.stream_batch_limit = 2;
  retention.durable_history_capacity = 5;
  naim::controller::TelemetryLiveStore::AlertThresholds thresholds;
  store.ConfigureOperationalPolicy(retention, thresholds);
  const auto db_path =
      std::filesystem::temp_directory_path() /
      ("naim-telemetry-recovered-counters-" + std::to_string(CurrentMillis()) + ".sqlite");
  store.ConfigurePersistence(db_path.string(), retention.durable_history_capacity);

  const auto now = CurrentMillis();
  for (std::uint64_t index = 0; index < 6; ++index) {
    auto frame = MakeFrame("node-recovered", "plane-recovered", now - 5000 + index);
    frame.adaptive_interval_ms = 10000;
    frame.adaptive_reason = "idle-no-plane";
    frame.publish_error_count = 2;
    frame.last_publish_error.clear();
    Expect(store.Upsert(frame), "recovered counter frame should update");
  }

  const auto health = store.BuildHealth(std::string("plane-recovered"));
  Expect(
      health.at("status").get<std::string>() == "ok",
      "retention pruning and recovered publish counters should not degrade health");
  Expect(health.at("alerts").empty(), "recovered counters should not produce alerts");
  Expect(
      health.at("planes").at(0).at("status").get<std::string>() == "ok",
      "plane status should not degrade on retention pruning alone");

  const auto snapshot = store.BuildSnapshot(std::string("plane-recovered"), 0);
  Expect(
      snapshot.at("nodes").at(0).at("telemetry_health").at("status").get<std::string>() == "ok",
      "node health should not degrade on recovered publish counters");
  std::filesystem::remove(db_path);
  store.ResetForTests();
  std::cout << "ok: telemetry-live-store-recovered-counters-health\n";
}

void TestGpuUnavailableStorageNodeDoesNotDegradeHealth() {
  auto& store = naim::controller::TelemetryLiveStore::Instance();
  store.ResetForTests();
  const auto db_path =
      std::filesystem::temp_directory_path() /
      ("naim-telemetry-storage-gpu-health-" + std::to_string(CurrentMillis()) + ".sqlite");
  store.ConfigurePersistence(db_path.string(), 8);

  auto frame = MakeFrame("node-storage-gpuless", "plane-storage-gpuless", CurrentMillis());
  frame.ttl_ms = 60000;
  frame.adaptive_interval_ms = 10000;
  frame.adaptive_reason = "idle-no-plane";
  frame.degraded_reason = "gpu:unavailable";
  frame.gpu.degraded = true;
  frame.gpu.devices.clear();
  frame.instance_runtime.clear();
  Expect(store.Upsert(frame), "gpuless storage frame should update");

  const auto health = store.BuildHealth(std::string("plane-storage-gpuless"));
  Expect(
      health.at("status").get<std::string>() == "ok",
      "gpuless storage node should not degrade global health");
  Expect(
      health.at("planes").at(0).at("status").get<std::string>() == "ok",
      "gpuless storage node should not degrade plane health");
  Expect(
      health.at("planes").at(0).at("degraded_nodes").get<std::uint64_t>() == 0,
      "gpuless storage node should not count as degraded");

  const auto snapshot = store.BuildSnapshot(std::string("plane-storage-gpuless"), 0);
  const auto node = snapshot.at("nodes").at(0);
  Expect(
      node.at("telemetry_health").at("status").get<std::string>() == "ok",
      "gpuless storage node telemetry health should remain ok");
  Expect(
      node.at("telemetry_health_status").get<std::string>() == "ok",
      "gpuless storage frame health status should remain ok");

  auto gpu_runtime_frame =
      MakeFrame("node-gpu-runtime", "plane-gpu-runtime", CurrentMillis());
  gpu_runtime_frame.ttl_ms = 60000;
  gpu_runtime_frame.degraded_reason = "gpu:unavailable";
  gpu_runtime_frame.gpu.devices.clear();
  gpu_runtime_frame.instance_runtime.push_back(naim::RuntimeProcessStatus{
      "worker-0",
      "worker",
      "node-gpu-runtime",
      "/models/qwen",
      "0",
      "running",
      "",
      "",
      0,
      0,
      false,
  });
  Expect(store.Upsert(gpu_runtime_frame), "gpu-bound runtime frame should update");
  const auto gpu_runtime_health = store.BuildHealth(std::string("plane-gpu-runtime"));
  Expect(
      gpu_runtime_health.at("planes").at(0).at("status").get<std::string>() == "degraded",
      "gpu-bound runtime should still degrade when gpu is unavailable");

  std::filesystem::remove(db_path);
  store.ResetForTests();
  std::cout << "ok: telemetry-live-store-storage-gpu-health\n";
}

void TestStreamMetricsAndOpenMetrics() {
  auto& store = naim::controller::TelemetryLiveStore::Instance();
  store.ResetForTests();
  const auto replay_db_path =
      (std::filesystem::temp_directory_path() / "naim-telemetry-replay-health-test.sqlite").string();
  store.ConfigurePersistence(replay_db_path);
  Expect(store.Upsert(MakeFrame("node-replay", "plane-replay", CurrentMillis())), "replay frame");
  store.RecordStreamReplayRequired("live");
  const auto replay_health = store.BuildHealth(std::string("plane-replay"));
  Expect(
      replay_health.at("status").get<std::string>() == "ok",
      "stream replay alone should not degrade telemetry health");
  std::filesystem::remove(replay_db_path);
  store.ResetForTests();

  Expect(store.Upsert(MakeFrame("node-metrics", "plane-metrics", CurrentMillis())), "metrics frame");
  store.RecordStreamOpened("live");
  store.RecordStreamOpened("live");
  store.RecordStreamClosed("live");
  store.RecordStreamReplayRequired("live");
  store.RecordStreamSendFailure("live");
  const auto health = store.BuildHealth(std::string("plane-metrics"));
  const auto live = health.at("streams").at("live");
  Expect(live.at("active_clients").get<std::uint64_t>() == 1, "live active clients");
  Expect(live.at("reconnect_total").get<std::uint64_t>() == 1, "live reconnect count");
  Expect(live.at("replay_required_total").get<std::uint64_t>() == 1, "live replay count");
  Expect(live.at("send_failure_total").get<std::uint64_t>() == 1, "live send failure count");
  const auto metrics = store.BuildOpenMetrics(std::string("plane-metrics"));
  Expect(
      metrics.find("naim_telemetry_health_status") != std::string::npos,
      "openmetrics should expose health status");
  Expect(
      metrics.find("naim_telemetry_stream_active_clients") != std::string::npos,
      "openmetrics should expose stream clients");
  store.ResetForTests();
  std::cout << "ok: telemetry-live-store-stream-openmetrics\n";
}

}  // namespace

int main() {
  try {
    TestUpsertAndSnapshot();
    TestPlaneFilter();
    TestCoherenceFieldsAndStaleProjection();
    TestBackpressureProjection();
    TestPlaneAggregatesAndDownsampling();
    TestSqlitePersistenceReplayAndHealth();
    TestConfigurableRetentionAndMetrics();
    TestRecoveredTelemetryCountersDoNotDegradeHealth();
    TestGpuUnavailableStorageNodeDoesNotDegradeHealth();
    TestStreamMetricsAndOpenMetrics();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "telemetry_live_store_tests failed: " << error.what() << "\n";
    return 1;
  }
}
