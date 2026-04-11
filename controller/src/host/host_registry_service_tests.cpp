#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "naim/runtime/runtime_status.h"
#include "naim/state/sqlite_store.h"
#include "host/host_registry_service.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

void Expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string MakeTempDbPath(const std::string& test_name) {
  const fs::path root = fs::temp_directory_path() / "naim-host-registry-tests" / test_name;
  std::error_code error;
  fs::remove_all(root, error);
  fs::create_directories(root);
  return (root / "controller.sqlite").string();
}

void SeedHost(
    naim::ControllerStore& store,
    const std::string& node_name,
    const std::string& storage_root) {
  naim::RegisteredHostRecord host;
  host.node_name = node_name;
  host.registration_state = "registered";
  host.onboarding_state = "completed";
  host.session_state = "connected";
  host.transport_mode = "out";
  host.execution_mode = "mixed";
  host.capabilities_json = json{{"storage_root", storage_root}}.dump();
  store.UpsertRegisteredHost(host);
}

void SeedObservation(
    naim::ControllerStore& store,
    const std::string& node_name,
    const std::string& storage_root,
    int gpu_count,
    std::uint64_t total_memory_bytes,
    std::uint64_t storage_total_bytes,
    std::uint64_t storage_free_bytes) {
  naim::HostObservation observation;
  observation.node_name = node_name;
  observation.status = naim::HostObservationStatus::Idle;
  observation.heartbeat_at = "2026-04-09 11:30:00";

  naim::GpuTelemetrySnapshot gpu;
  gpu.source = "test";
  gpu.collected_at = observation.heartbeat_at;
  for (int index = 0; index < gpu_count; ++index) {
    gpu.devices.push_back(naim::GpuDeviceTelemetry{
        std::to_string(index),
        24576,
        0,
        24576,
        0,
        0,
        false,
        {},
    });
  }
  observation.gpu_telemetry_json = naim::SerializeGpuTelemetryJson(gpu);

  naim::CpuTelemetrySnapshot cpu;
  cpu.source = "test";
  cpu.collected_at = observation.heartbeat_at;
  cpu.total_memory_bytes = total_memory_bytes;
  cpu.available_memory_bytes = total_memory_bytes;
  cpu.used_memory_bytes = 0;
  observation.cpu_telemetry_json = naim::SerializeCpuTelemetryJson(cpu);

  naim::DiskTelemetrySnapshot disk;
  disk.source = "test";
  disk.collected_at = observation.heartbeat_at;
  naim::DiskTelemetryRecord record;
  record.disk_name = "storage-root";
  record.node_name = node_name;
  record.mount_point = storage_root;
  record.total_bytes = storage_total_bytes;
  record.free_bytes = storage_free_bytes;
  record.used_bytes = storage_total_bytes > storage_free_bytes
                          ? storage_total_bytes - storage_free_bytes
                          : 0;
  disk.items.push_back(record);
  observation.disk_telemetry_json = naim::SerializeDiskTelemetryJson(disk);

  store.UpsertHostObservation(observation);
}

json LoadSingleHostPayload(
    const std::string& test_name,
    int gpu_count,
    std::uint64_t total_memory_bytes,
    std::uint64_t storage_total_bytes) {
  const std::string db_path = MakeTempDbPath(test_name);
  naim::ControllerStore store(db_path);
  store.Initialize();
  SeedHost(store, test_name, "/srv/" + test_name);
  SeedObservation(
      store,
      test_name,
      "/srv/" + test_name,
      gpu_count,
      total_memory_bytes,
      storage_total_bytes,
      storage_total_bytes / 2);

  const naim::controller::HostRegistryService service(
      db_path,
      [](naim::ControllerStore&,
         const std::string&,
         const std::string&,
         const json&,
         const std::string&,
         const std::string&) {});
  const json payload = service.BuildPayload(test_name);
  Expect(payload.at("items").size() == 1, "expected one host registry item");
  return payload.at("items").at(0);
}

void TestDerivesStorageRole() {
  constexpr std::uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;
  const json item = LoadSingleHostPayload(
      "storage-node",
      0,
      16ULL * kGiB,
      200ULL * kGiB);
  Expect(item.at("derived_role").get<std::string>() == "storage", "expected storage role");
  Expect(item.at("role_eligible").get<bool>(), "storage role should be eligible");
}

void TestDerivesWorkerRole() {
  constexpr std::uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;
  const json item = LoadSingleHostPayload(
      "worker-node",
      2,
      128ULL * kGiB,
      500ULL * kGiB);
  Expect(item.at("derived_role").get<std::string>() == "worker", "expected worker role");
  Expect(item.at("role_eligible").get<bool>(), "worker role should be eligible");
}

void TestDerivesIneligibleRole() {
  constexpr std::uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;
  const json item = LoadSingleHostPayload(
      "ineligible-node",
      0,
      48ULL * kGiB,
      200ULL * kGiB);
  Expect(item.at("derived_role").get<std::string>() == "ineligible", "expected ineligible role");
  Expect(
      item.at("role_reason").get<std::string>() ==
          "no gpu and ram outside storage threshold",
      "expected ineligible reason for mid-range RAM");
  Expect(!item.at("role_eligible").get<bool>(), "ineligible role should not be eligible");
}

}  // namespace

int main() {
  TestDerivesStorageRole();
  TestDerivesWorkerRole();
  TestDerivesIneligibleRole();
  std::cout << "host registry service tests passed\n";
  return 0;
}
