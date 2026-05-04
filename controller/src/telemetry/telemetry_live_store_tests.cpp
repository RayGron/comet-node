#include "telemetry/telemetry_live_store.h"

#include <chrono>
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
  frame.plane_name = plane_name;
  frame.sequence = sequence;
  frame.sampled_at = "2026-05-04 12:00:00";
  frame.collected_at = frame.sampled_at;
  frame.expires_at = "2026-05-04 12:00:10";
  frame.monotonic_ms = sequence;
  frame.lane = "fast";
  frame.collector_duration_ms = 2;
  frame.publish_duration_ms = 3;
  frame.publisher_queue_delay_ms = 4;
  frame.telemetry_bus_depth = 1;
  frame.telemetry_dropped_frames = 0;
  frame.adaptive_interval_ms = 2000;
  frame.adaptive_reason = "test";
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
  Expect(node.at("channel").get<std::string>() == "host.telemetry.v1", "channel should roundtrip");
  Expect(node.at("lane").get<std::string>() == "full", "lane should roundtrip");
  Expect(node.at("monotonic_ms").get<std::uint64_t>() == 1234, "monotonic_ms should roundtrip");
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
      node.contains("controller_ingest_delay_ms"),
      "controller ingest delay should be projected");
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
  std::cout << "ok: telemetry-live-store-backpressure\n";
}

}  // namespace

int main() {
  try {
    TestUpsertAndSnapshot();
    TestPlaneFilter();
    TestCoherenceFieldsAndStaleProjection();
    TestBackpressureProjection();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "telemetry_live_store_tests failed: " << error.what() << "\n";
    return 1;
  }
}
