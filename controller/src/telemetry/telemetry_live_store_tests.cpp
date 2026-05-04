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

}  // namespace

int main() {
  try {
    TestUpsertAndSnapshot();
    TestPlaneFilter();
    TestCoherenceFieldsAndStaleProjection();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "telemetry_live_store_tests failed: " << error.what() << "\n";
    return 1;
  }
}
