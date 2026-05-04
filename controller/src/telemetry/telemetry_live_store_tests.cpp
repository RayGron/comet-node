#include "telemetry/telemetry_live_store.h"

#include <iostream>
#include <stdexcept>

namespace {

void Expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
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

}  // namespace

int main() {
  try {
    TestUpsertAndSnapshot();
    TestPlaneFilter();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "telemetry_live_store_tests failed: " << error.what() << "\n";
    return 1;
  }
}
