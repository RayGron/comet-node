#include "app/hostd_system_telemetry_collector.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <iomanip>

#include <nlohmann/json.hpp>

#if !defined(_WIN32)
#include <dlfcn.h>
#include <sys/statvfs.h>
#endif

#ifdef NAIM_RUNTIME_CUDA
#include <cstdlib>
#endif

#ifdef NAIM_RUNTIME_VULKAN
#include <vulkan/vulkan.h>
#endif

namespace naim::hostd {

namespace fs = std::filesystem;

namespace {

std::string CurrentTimestampString() {
  const std::time_t now = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &now);
#else
  localtime_r(&now, &tm);
#endif
  char buffer[32];
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm) == 0) {
    return {};
  }
  return buffer;
}

struct BlockDeviceIoStats {
  std::uint64_t read_ios = 0;
  std::uint64_t read_sectors = 0;
  std::uint64_t write_ios = 0;
  std::uint64_t write_sectors = 0;
  std::uint64_t io_in_progress = 0;
  std::uint64_t io_time_ms = 0;
  std::uint64_t weighted_io_time_ms = 0;
};

std::optional<std::string> BlockDeviceNameFromPath(const std::string& device_path) {
  if (device_path.empty()) {
    return std::nullopt;
  }
  const auto device_name = fs::path(device_path).filename().string();
  if (device_name.empty()) {
    return std::nullopt;
  }
  return device_name;
}

std::optional<bool> ReadBlockDeviceReadOnly(const std::string& device_path) {
  const auto device_name = BlockDeviceNameFromPath(device_path);
  if (!device_name.has_value()) {
    return std::nullopt;
  }
  std::ifstream input("/sys/class/block/" + *device_name + "/ro");
  if (!input.is_open()) {
    return std::nullopt;
  }
  int value = 0;
  if (!(input >> value)) {
    return std::nullopt;
  }
  return value != 0;
}

std::optional<std::uint64_t> ReadBlockDeviceIoErrorCount(const std::string& device_path) {
  const auto device_name = BlockDeviceNameFromPath(device_path);
  if (!device_name.has_value()) {
    return std::nullopt;
  }
  const std::array<fs::path, 2> candidates{
      fs::path("/sys/class/block") / *device_name / "ioerr_cnt",
      fs::path("/sys/class/block") / *device_name / "device" / "ioerr_cnt",
  };
  for (const auto& candidate : candidates) {
    std::ifstream input(candidate);
    if (!input.is_open()) {
      continue;
    }
    std::uint64_t value = 0;
    if (input >> value) {
      return value;
    }
  }
  return std::nullopt;
}

std::optional<BlockDeviceIoStats> ReadBlockDeviceIoStats(const std::string& device_path) {
  const auto device_name = BlockDeviceNameFromPath(device_path);
  if (!device_name.has_value()) {
    return std::nullopt;
  }
  std::ifstream input("/sys/class/block/" + *device_name + "/stat");
  if (!input.is_open()) {
    return std::nullopt;
  }

  BlockDeviceIoStats stats;
  std::uint64_t reads_merged = 0;
  std::uint64_t read_time_ms = 0;
  std::uint64_t writes_merged = 0;
  std::uint64_t write_time_ms = 0;
  if (!(input >> stats.read_ios >> reads_merged >> stats.read_sectors >> read_time_ms >>
        stats.write_ios >> writes_merged >> stats.write_sectors >> write_time_ms >>
        stats.io_in_progress >> stats.io_time_ms >> stats.weighted_io_time_ms)) {
    return std::nullopt;
  }
  return stats;
}

struct NvmlMemoryInfo {
  unsigned long long total = 0;
  unsigned long long free = 0;
  unsigned long long used = 0;
};

struct NvmlUtilizationInfo {
  unsigned int gpu = 0;
  unsigned int memory = 0;
};

std::optional<std::string> ReadTrimmedFile(const fs::path& path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    return std::nullopt;
  }
  std::string value;
  std::getline(input, value);
  while (!value.empty() &&
         (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')) {
    value.pop_back();
  }
  return value;
}

std::map<std::string, std::vector<std::string>> LoadInterfaceAddresses(
    const HostdCommandSupport& command_support) {
  std::map<std::string, std::vector<std::string>> addresses_by_interface;
#if defined(_WIN32)
  (void)command_support;
#else
  const std::string output =
      command_support.RunCommandCapture("ip -o -4 addr show 2>/dev/null || true");
  std::istringstream lines(output);
  std::string line;
  while (std::getline(lines, line)) {
    std::istringstream fields(line);
    std::string index;
    std::string interface_name;
    std::string family;
    std::string address;
    fields >> index >> interface_name >> family >> address;
    if (interface_name.empty() || family != "inet" || address.empty()) {
      continue;
    }
    if (!interface_name.empty() && interface_name.back() == ':') {
      interface_name.pop_back();
    }
    addresses_by_interface[interface_name].push_back(address);
  }
#endif
  return addresses_by_interface;
}

std::vector<naim::PeerDiscoveryTelemetry> LoadPeerDiscoveryTelemetry(
    const std::string& state_root) {
  std::vector<naim::PeerDiscoveryTelemetry> peers;
  if (state_root.empty()) {
    return peers;
  }
  const fs::path peer_path = fs::path(state_root) / "peer-discovery.json";
  std::ifstream input(peer_path);
  if (!input.is_open()) {
    return peers;
  }
  const auto payload = nlohmann::json::parse(input, nullptr, false);
  if (payload.is_discarded() || !payload.is_object()) {
    return peers;
  }
  for (const auto& value : payload.value("peers", nlohmann::json::array())) {
    if (!value.is_object()) {
      continue;
    }
    naim::PeerDiscoveryTelemetry peer;
    peer.peer_node_name = value.value("peer_node_name", std::string{});
    peer.peer_endpoint = value.value("peer_endpoint", std::string{});
    peer.local_interface = value.value("local_interface", std::string{});
    peer.remote_address = value.value("remote_address", std::string{});
    peer.seen_udp = value.value("seen_udp", false);
    peer.tcp_reachable = value.value("tcp_reachable", false);
    peer.rtt_ms = value.value("rtt_ms", 0);
    peer.last_seen_at = value.value("last_seen_at", std::string{});
    peer.last_probe_at = value.value("last_probe_at", std::string{});
    if (!peer.peer_node_name.empty()) {
      peers.push_back(std::move(peer));
    }
  }
  return peers;
}

std::uint64_t ReadUint64FileOrZero(const fs::path& path) {
  std::ifstream input(path);
  std::uint64_t value = 0;
  if (!input.is_open()) {
    return 0;
  }
  input >> value;
  return value;
}

std::string Lowercase(std::string value) {
  std::transform(
      value.begin(),
      value.end(),
      value.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

bool ContainsAnyToken(const std::string& text, const std::initializer_list<const char*> tokens) {
  for (const char* token : tokens) {
    if (text.find(token) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::optional<double> ReadTemperatureCelsius(const fs::path& path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    return std::nullopt;
  }
  double value = 0.0;
  if (!(input >> value)) {
    return std::nullopt;
  }
  if (value > 1000.0) {
    value /= 1000.0;
  }
  if (value < -50.0 || value > 200.0) {
    return std::nullopt;
  }
  return value;
}

std::vector<double> CollectCpuTemperatureSamples() {
  std::vector<double> samples;

  const fs::path hwmon_root("/sys/class/hwmon");
  if (fs::exists(hwmon_root)) {
    for (const auto& hwmon_entry : fs::directory_iterator(hwmon_root)) {
      if (!hwmon_entry.is_directory() && !hwmon_entry.is_symlink()) {
        continue;
      }
      const std::string hwmon_name =
          Lowercase(ReadTrimmedFile(hwmon_entry.path() / "name").value_or(std::string{}));
      const bool hwmon_cpu_related = ContainsAnyToken(
          hwmon_name,
          {"coretemp", "k10temp", "zenpower", "cpu", "package", "acpitz", "soc"});
      for (const auto& sensor_entry : fs::directory_iterator(hwmon_entry.path())) {
        const std::string file_name = sensor_entry.path().filename().string();
        if (file_name.rfind("temp", 0) != 0 ||
            file_name.find("_input") == std::string::npos) {
          continue;
        }
        const std::string label_file =
            file_name.substr(0, file_name.find("_input")) + "_label";
        const std::string label = Lowercase(
            ReadTrimmedFile(hwmon_entry.path() / label_file).value_or(std::string{}));
        const bool label_cpu_related = ContainsAnyToken(
            label,
            {"package", "cpu", "core", "tctl", "tdie", "ccd", "soc"});
        if (!hwmon_cpu_related && !label_cpu_related) {
          continue;
        }
        const auto sample = ReadTemperatureCelsius(sensor_entry.path());
        if (sample.has_value()) {
          samples.push_back(*sample);
        }
      }
    }
  }

  if (!samples.empty()) {
    return samples;
  }

  const fs::path thermal_root("/sys/class/thermal");
  if (fs::exists(thermal_root)) {
    for (const auto& zone_entry : fs::directory_iterator(thermal_root)) {
      const std::string zone_name = zone_entry.path().filename().string();
      if (zone_name.rfind("thermal_zone", 0) != 0) {
        continue;
      }
      const std::string zone_type = Lowercase(
          ReadTrimmedFile(zone_entry.path() / "type").value_or(std::string{}));
      if (!ContainsAnyToken(zone_type, {"cpu", "pkg", "x86", "acpitz", "soc"})) {
        continue;
      }
      const auto sample = ReadTemperatureCelsius(zone_entry.path() / "temp");
      if (sample.has_value()) {
        samples.push_back(*sample);
      }
    }
  }

  return samples;
}

std::optional<HostdCpuCounterSample> ReadCpuSample() {
  std::ifstream input("/proc/stat");
  if (!input.is_open()) {
    return std::nullopt;
  }

  std::string cpu_label;
  HostdCpuCounterSample sample;
  std::uint64_t user = 0;
  std::uint64_t nice = 0;
  std::uint64_t system = 0;
  std::uint64_t idle = 0;
  std::uint64_t iowait = 0;
  std::uint64_t irq = 0;
  std::uint64_t softirq = 0;
  std::uint64_t steal = 0;
  input >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
  if (!input.good() || cpu_label != "cpu") {
    return std::nullopt;
  }
  sample.idle = idle + iowait;
  sample.total = user + nice + system + idle + iowait + irq + softirq + steal;
  return sample;
}

std::optional<std::array<double, 3>> ReadLoadAverage() {
  std::ifstream input("/proc/loadavg");
  if (!input.is_open()) {
    return std::nullopt;
  }
  std::array<double, 3> load{0.0, 0.0, 0.0};
  input >> load[0] >> load[1] >> load[2];
  if (!input.good()) {
    return std::nullopt;
  }
  return load;
}

}  // namespace

naim::GpuTelemetrySnapshot HostdSystemTelemetryCollector::CollectGpuTelemetry(
  const naim::DesiredState& state,
  const std::string& node_name,
  const std::vector<naim::RuntimeProcessStatus>& instance_statuses) const {

#ifdef NAIM_RUNTIME_CUDA

  const bool disable_nvml =
    std::getenv("NAIM_DISABLE_NVML") != nullptr &&
    std::string(std::getenv("NAIM_DISABLE_NVML")) != "0";

  if (!disable_nvml) {
    if (const auto nvml_snapshot = collectGpuTelemetryWithNVML(state, node_name);
        nvml_snapshot.has_value()) {
      naim::GpuTelemetrySnapshot snapshot = *nvml_snapshot;
      populateGpuProcessesFromNvidiaSMI(&snapshot, instance_statuses);
      snapshot.contract_version = 1;
      snapshot.collected_at = CurrentTimestampString();
      return snapshot;
    }
  }

  if (const auto smi_snapshot = collectGpuTelemetryWithNvidiaSMI(state,
                                                                 node_name,
                                                                 instance_statuses);
      smi_snapshot.has_value()) {

    naim::GpuTelemetrySnapshot snapshot = *smi_snapshot;
    snapshot.contract_version = 1;
    snapshot.collected_at = CurrentTimestampString();
    return snapshot;
  }
#endif

  // Vulkan section
#ifdef NAIM_RUNTIME_VULKAN

  (void)instance_statuses;
  (void)node_name;
  (void)state;

  if (const auto smi_snapshot = collectGpuTelemetryWithVulkanAPI();
      smi_snapshot.has_value()) {

    naim::GpuTelemetrySnapshot snapshot = *smi_snapshot;
    snapshot.contract_version = 1;
    snapshot.collected_at = CurrentTimestampString();
    return snapshot;
  }

#endif


  naim::GpuTelemetrySnapshot snapshot;
  snapshot.contract_version = 1;
  snapshot.degraded = true;
  snapshot.source = "unavailable";
  snapshot.collected_at = CurrentTimestampString();
  return snapshot;
}

#ifdef _WIN32
std::wstring ToWString(const std::string& str) {
  if (str.empty()) return L"";
  int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
  std::wstring wstrTo(size_needed, 0);
  MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
  return wstrTo;
}

std::string FromWString(const std::wstring& wstr) {
  if (wstr.empty()) return "";
  int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
  std::string strTo(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
  return strTo;
}
#endif

struct MountInfo {
  std::string source;
  std::string fs_type;
};

std::optional<MountInfo> GetMountInfo(const std::string& mount_point) {
#if defined(_WIN32)
  std::wstring wpath = ToWString(path);
  if (!wpath.empty() && wpath.back() != L'\\' && wpath.back() != L'/') {
    wpath += L'\\';
  }

  WCHAR volume_name[MAX_PATH];
  WCHAR file_system_name[MAX_PATH];
  DWORD serial_number, max_component_len, file_system_flags;

  // GetVolumeInformation получает имя файловой системы (NTFS/FAT32)
  if (GetVolumeInformationW(
        wpath.c_str(),
        NULL, 0, // Нам не нужна метка тома (Volume Label)
        &serial_number,
        &max_component_len,
        &file_system_flags,
        file_system_name,
        MAX_PATH)) {

    MountInfo info;
    info.fs_type = FromWString(file_system_name);

    if (GetVolumeNameForVolumeMountPointW(wpath.c_str(), volume_name, MAX_PATH)) {
      info.source = FromWString(volume_name);
    } else {
      info.source = "Local Disk";
    }

    return info;
  }
  return std::nullopt;
#else
  std::ifstream input("/proc/self/mounts");
  if (!input.is_open()) return std::nullopt;

  std::string src, target, type;
  while (input >> src >> target >> type) {
    std::string dummy;
    std::getline(input, dummy);
    if (target == mount_point) {
      return MountInfo{src, type};
    }
  }
  return std::nullopt;
#endif
}

bool IsPathMounted(const std::string& path) {
#if defined(_WIN32)
  std::wstring wpath = ToWString(path);

  if (!wpath.empty() && wpath.back() != L'\\' && wpath.back() != L'/') {
    wpath += L'\\';
  }

  UINT drive_type = GetDriveTypeW(wpath.c_str());
  return (drive_type != DRIVE_UNKNOWN && drive_type != DRIVE_NO_ROOT_DIR);
#else
  return GetMountInfo(path).has_value();
#endif
}

naim::DiskTelemetrySnapshot HostdSystemTelemetryCollector::CollectDiskTelemetry(
  const naim::DesiredState& state,
  const std::string& node_name) const {

  naim::DiskTelemetrySnapshot snapshot;
  snapshot.contract_version = 1;
  snapshot.source = "std::filesystem::space";
  snapshot.collected_at = CurrentTimestampString();

  for (const auto& disk : state.disks) {
    if (disk.node_name != node_name) {
      continue;
    }

    naim::DiskTelemetryRecord record;
    record.disk_name = disk.name;
    record.plane_name = disk.plane_name;
    record.node_name = disk.node_name;
    record.mount_point = disk.host_path;

    std::error_code ec;
    bool exists = fs::exists(disk.host_path, ec);

    record.runtime_state = exists ? "present" : "missing";
    record.health = exists ? "ok" : "missing";

    if (!exists) {
      record.fault_count += 1;
      record.fault_reasons.push_back("host-path-missing");
    }

    if (IsPathMounted(disk.host_path)) {
      record.mounted = true;
      record.runtime_state = "mounted";

      if (auto mount_info = GetMountInfo(disk.host_path)) {
        record.filesystem_type = mount_info->fs_type;
        record.mount_source = mount_info->source;

        if (record.mount_source.rfind("/dev/", 0) == 0) {
          CollectBlockDeviceStats(record, record.mount_source);
        }
      } else {
        record.warning_count += 1;
        record.fault_reasons.push_back("mount-source-unavailable");
        if (record.health == "ok") record.health = "degraded";
      }
    }

    fs::space_info space = fs::space(disk.host_path, ec);
    if (!ec) {
      record.total_bytes = space.capacity;
      record.free_bytes = space.available;
      record.used_bytes = (space.capacity >= space.available)
                            ? (space.capacity - space.available) : 0;

      if (record.health == "missing") record.health = "ok";
      if (record.runtime_state == "missing") record.runtime_state = "available";
    } else {
      record.status_message = record.status_message.empty()
      ? "fs::space failed"
      : record.status_message + "; fs::space failed";

      record.fault_count += 1;
      record.fault_reasons.push_back("filesystem-space-unavailable");
      if (record.health == "ok") record.health = "degraded";
    }

    snapshot.items.push_back(std::move(record));
  }

  return snapshot;
}

void HostdSystemTelemetryCollector::CollectBlockDeviceStats(
  naim::DiskTelemetryRecord& record,
  const std::string& source) const {

  // ReadBlockDeviceReadOnly
  if (auto ro = ReadBlockDeviceReadOnly(source)) {
    record.read_only = *ro;
    if (*ro) {
      record.warning_count += 1;
      record.fault_reasons.push_back("read-only-device");
      if (record.health == "ok") record.health = "degraded";
    }
  }

  // ReadBlockDeviceIoStats
  if (auto io_stats = ReadBlockDeviceIoStats(source)) {
    record.perf_counters_available = true;
    record.read_ios = io_stats->read_ios;
    record.write_ios = io_stats->write_ios;
    record.read_bytes = io_stats->read_sectors * 512ULL;
    record.write_bytes = io_stats->write_sectors * 512ULL;
    record.io_in_progress = static_cast<int>(io_stats->io_in_progress);
    record.io_time_ms = io_stats->io_time_ms;
    record.weighted_io_time_ms = io_stats->weighted_io_time_ms;
  }

  // ReadBlockDeviceIoErrorCount
  if (auto errors = ReadBlockDeviceIoErrorCount(source)) {
    record.io_error_counters_available = true;
    record.io_error_count = *errors;
    if (*errors > 0) {
      record.fault_count += 1;
      record.fault_reasons.push_back("io-error-count-nonzero");
      if (record.health == "ok") record.health = "degraded";
    }
  }
}

naim::DiskTelemetryRecord HostdSystemTelemetryCollector::BuildStorageRootTelemetry(
    const std::string& node_name,
    const std::string& storage_root) const {
  naim::DiskTelemetryRecord record;
  record.disk_name = "storage-root";
  record.node_name = node_name;
  record.mount_point = storage_root;
  record.runtime_state = fs::exists(storage_root) ? "present" : "missing";
  record.health = fs::exists(storage_root) ? "ok" : "missing";
  if (record.health == "missing") {
    record.fault_count += 1;
    record.fault_reasons.push_back("storage-root-missing");
  }

  if (IsPathMounted(storage_root)) {
    record.mounted = true;
    record.runtime_state = "mounted";
    if (auto mount_info = GetMountInfo(storage_root)) {
      record.filesystem_type = mount_info->fs_type;
      record.mount_source = mount_info->source;

      if (mount_info->source.rfind("/dev/", 0) == 0) {
        if (const auto read_only = ReadBlockDeviceReadOnly(mount_info->source);
            read_only.has_value()) {
          record.read_only = *read_only;
        }
        if (const auto io_stats = ReadBlockDeviceIoStats(mount_info->source); io_stats.has_value()) {
          record.perf_counters_available = true;
          record.read_ios = io_stats->read_ios;
          record.write_ios = io_stats->write_ios;
          record.read_bytes = io_stats->read_sectors * 512ULL;
          record.write_bytes = io_stats->write_sectors * 512ULL;
          record.io_in_progress = static_cast<int>(io_stats->io_in_progress);
          record.io_time_ms = io_stats->io_time_ms;
          record.weighted_io_time_ms = io_stats->weighted_io_time_ms;
        }
        if (const auto io_error_count = ReadBlockDeviceIoErrorCount(mount_info->source);
            io_error_count.has_value()) {
          record.io_error_counters_available = true;
          record.io_error_count = *io_error_count;
        }
      }
    }
  }

  std::error_code space_error;
  const auto space_info = fs::space(storage_root, space_error);
  if (!space_error) {
    record.total_bytes = space_info.capacity;
    record.free_bytes = space_info.available;
    record.used_bytes =
      record.total_bytes >= record.free_bytes ? (record.total_bytes - record.free_bytes) : 0;
  }

  return record;
}

naim::CpuTelemetrySnapshot HostdSystemTelemetryCollector::CollectCpuTelemetry() const {
  naim::CpuTelemetrySnapshot snapshot;
  snapshot.contract_version = 1;
  snapshot.source = "procfs";
  snapshot.collected_at = CurrentTimestampString();
  snapshot.core_count = static_cast<int>(std::thread::hardware_concurrency());

  const auto first = ReadCpuSample();
  if (first.has_value() && last_cpu_sample_.has_value() &&
      first->total > last_cpu_sample_->total) {
    const auto total_delta = static_cast<double>(first->total - last_cpu_sample_->total);
    const auto idle_delta = static_cast<double>(first->idle - last_cpu_sample_->idle);
    snapshot.utilization_pct =
        total_delta > 0.0 ? std::max(0.0, 100.0 * (1.0 - (idle_delta / total_delta))) : 0.0;
    last_cpu_sample_ = first;
  } else if (first.has_value()) {
    last_cpu_sample_ = first;
    snapshot.degraded = true;
    snapshot.source = "procfs-warmup";
  } else {
    snapshot.degraded = true;
    snapshot.source = "procfs-unavailable";
  }

  if (const auto load = ReadLoadAverage(); load.has_value()) {
    snapshot.loadavg_1m = (*load)[0];
    snapshot.loadavg_5m = (*load)[1];
    snapshot.loadavg_15m = (*load)[2];
  } else {
    snapshot.degraded = true;
  }

  const auto temperature_samples = CollectCpuTemperatureSamples();
  if (!temperature_samples.empty()) {
    double total_temperature = 0.0;
    double max_temperature = temperature_samples.front();
    for (double sample : temperature_samples) {
      total_temperature += sample;
      max_temperature = std::max(max_temperature, sample);
    }
    snapshot.temperature_available = true;
    snapshot.temperature_c =
        total_temperature / static_cast<double>(temperature_samples.size());
    snapshot.max_temperature_c = max_temperature;
  }

  std::ifstream meminfo("/proc/meminfo");
  if (meminfo.is_open()) {
    std::string key;
    std::uint64_t value = 0;
    std::string unit;
    std::uint64_t total_kb = 0;
    std::uint64_t available_kb = 0;
    while (meminfo >> key >> value >> unit) {
      if (key == "MemTotal:") {
        total_kb = value;
      } else if (key == "MemAvailable:") {
        available_kb = value;
      }
    }
    snapshot.total_memory_bytes = total_kb * 1024ULL;
    snapshot.available_memory_bytes = available_kb * 1024ULL;
    snapshot.used_memory_bytes =
        snapshot.total_memory_bytes >= snapshot.available_memory_bytes
            ? (snapshot.total_memory_bytes - snapshot.available_memory_bytes)
            : 0;
  } else {
    snapshot.degraded = true;
  }

  return snapshot;
}

naim::NetworkTelemetrySnapshot HostdSystemTelemetryCollector::CollectNetworkTelemetry(
    const std::string& state_root) const {
  naim::NetworkTelemetrySnapshot snapshot;
  snapshot.contract_version = 1;
  snapshot.source = "sysfs";
  snapshot.collected_at = CurrentTimestampString();
  snapshot.peer_discovery = LoadPeerDiscoveryTelemetry(state_root);

  const fs::path net_root("/sys/class/net");
  if (!fs::exists(net_root)) {
    snapshot.degraded = true;
    snapshot.source = "unavailable";
    return snapshot;
  }

  const auto addresses_by_interface = LoadInterfaceAddresses(command_support_);
  for (const auto& entry : fs::directory_iterator(net_root)) {
    if (!entry.is_directory() && !entry.is_symlink()) {
      continue;
    }
    naim::NetworkInterfaceTelemetry interface;
    interface.interface_name = entry.path().filename().string();
    interface.oper_state =
        ReadTrimmedFile(entry.path() / "operstate").value_or(std::string{"unknown"});
    const auto carrier = ReadTrimmedFile(entry.path() / "carrier");
    if (carrier.has_value()) {
      interface.link_state = (*carrier == "1") ? "up" : "down";
    } else {
      interface.link_state = interface.oper_state;
    }
    interface.rx_bytes = ReadUint64FileOrZero(entry.path() / "statistics" / "rx_bytes");
    interface.tx_bytes = ReadUint64FileOrZero(entry.path() / "statistics" / "tx_bytes");
    interface.loopback = interface.interface_name == "lo";
    if (const auto address_it = addresses_by_interface.find(interface.interface_name);
        address_it != addresses_by_interface.end()) {
      interface.addresses = address_it->second;
    }
    snapshot.interfaces.push_back(std::move(interface));
  }

  std::sort(
      snapshot.interfaces.begin(),
      snapshot.interfaces.end(),
      [](const auto& left, const auto& right) { return left.interface_name < right.interface_name; });
  return snapshot;
}

std::vector<std::string> HostdSystemTelemetryCollector::splitCsvRow(
  const std::string &line) const{

  std::vector<std::string> result;
  std::stringstream stream(line);
  std::string current;

  while (std::getline(stream, current, ',')) {
    result.push_back(command_support_.Trim(current));
  }

  return result;
}

// Helper to get current ISO 8601 time
std::string getCurrentTimestamp() {
  auto now = std::chrono::system_clock::now();
  auto in_time_t = std::chrono::system_clock::to_time_t(now);
  std::stringstream ss;
  ss << std::put_time(std::gmtime(&in_time_t), "%Y-%m-%dT%H:%M:%SZ");
  return ss.str();
}

#ifdef NAIM_RUNTIME_VULKAN
std::optional<GpuTelemetrySnapshot> HostdSystemTelemetryCollector::collectGpuTelemetryWithVulkanAPI() const {
  GpuTelemetrySnapshot snapshot;
  snapshot.source = "Vulkan API (VK_EXT_memory_budget)";
  snapshot.collected_at = getCurrentTimestamp();

  // 1. Initialize Instance
  VkApplicationInfo appInfo = {};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.apiVersion = VK_API_VERSION_1_1; // Required for memory_budget

  VkInstanceCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;

  VkInstance instance;
  if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
    return std::nullopt;
  }

  // 2. Enumerate Physical Devices
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
  std::vector<VkPhysicalDevice> physDevices(deviceCount);
  vkEnumeratePhysicalDevices(instance, &deviceCount, physDevices.data());

  for (auto& physDevice : physDevices) {
    GpuDeviceTelemetry deviceData;

    // Get device name and properties
    VkPhysicalDeviceProperties deviceProps;
    vkGetPhysicalDeviceProperties(physDevice, &deviceProps);

    // collect only discret GPU cards
    if (deviceProps.deviceType != VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
      continue;
    }

    deviceData.gpu_device = deviceProps.deviceName;

    // 3. Query Memory Budget
    // Check if extension is supported (in production you'd check extension strings)
    VkPhysicalDeviceMemoryBudgetPropertiesEXT memBudgetProps = {};
    memBudgetProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;

    VkPhysicalDeviceMemoryProperties2 memProps2 = {};
    memProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    memProps2.pNext = &memBudgetProps;

    vkGetPhysicalDeviceMemoryProperties2(physDevice, &memProps2);

    uint64_t totalUsage = 0;
    uint64_t totalBudget = 0;

    // Vulkan divides memory into heaps (VRAM, GTT/RAM)
    for (uint32_t i = 0; i < memProps2.memoryProperties.memoryHeapCount; ++i) {
      // VK_MEMORY_HEAP_DEVICE_LOCAL_BIT indicates VRAM
      if (memProps2.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
        totalUsage += memBudgetProps.heapUsage[i];
        totalBudget += memBudgetProps.heapBudget[i];
      }
    }

    // Convert bytes to MB
    deviceData.total_vram_mb = static_cast<int>(totalBudget / (1024 * 1024));
    deviceData.used_vram_mb = static_cast<int>(totalUsage / (1024 * 1024));
    deviceData.free_vram_mb = std::max(0, deviceData.total_vram_mb - deviceData.used_vram_mb);

    // Vulkan standard API doesn't provide temperature/utilization without vendor-specific extensions
    deviceData.temperature_available = false;
    deviceData.gpu_utilization_pct = 0;

    snapshot.devices.push_back(deviceData);
  }

  vkDestroyInstance(instance, nullptr);

  if (snapshot.devices.empty()) {
    return std::nullopt;
  }

  return snapshot;
}
#endif

#ifdef NAIM_RUNTIME_CUDA

std::optional<GpuTelemetrySnapshot> HostdSystemTelemetryCollector::collectGpuTelemetryWithNvidiaSMI(
  const naim::DesiredState& state,
  const std::string& node_name,
  const std::vector<naim::RuntimeProcessStatus>& instance_statuses) const{

  (void)state;
  (void)node_name;
  const std::string output = command_support_.RunCommandCapture(
    "nvidia-smi --query-gpu=index,memory.total,memory.used,memory.free,utilization.gpu,temperature.gpu "
    "--format=csv,noheader,nounits 2>/dev/null");
  if (output.empty()) {
    return std::nullopt;
  }

  naim::GpuTelemetrySnapshot snapshot;
  snapshot.degraded = true;
  snapshot.source = "nvidia-smi";
  std::istringstream input(output);
  std::string line;
  while (std::getline(input, line)) {
    const auto columns = splitCsvRow(line);
    if (columns.size() < 5) {
      continue;
    }
    try {
      naim::GpuDeviceTelemetry device;
      device.gpu_device = columns[0];
      device.total_vram_mb = std::stoi(columns[1]);
      device.used_vram_mb = std::stoi(columns[2]);
      device.free_vram_mb = std::stoi(columns[3]);
      device.gpu_utilization_pct = std::stoi(columns[4]);
      if (columns.size() >= 6 && columns[5] != "[N/A]" && columns[5] != "N/A" &&
          !columns[5].empty()) {
        device.temperature_c = std::stoi(columns[5]);
        device.temperature_available = true;
      }
      snapshot.devices.push_back(std::move(device));
    } catch (const std::exception&) {
      continue;
    }
  }
  populateGpuProcessesFromNvidiaSMI(&snapshot, instance_statuses);
  return snapshot;
}

std::optional<GpuTelemetrySnapshot> HostdSystemTelemetryCollector::collectGpuTelemetryWithNVML(
  const naim::DesiredState& state,
  const std::string& node_name) const{

#if defined(_WIN32)
  (void)state;
  (void)node_name;
  return std::nullopt;
#else
  (void)state;
  (void)node_name;
  void* lib = dlopen("libnvidia-ml.so.1", RTLD_LAZY);
  if (lib == nullptr) {
    return std::nullopt;
  }

  using nvmlReturn_t = int;
  using nvmlDevice_t = void*;
  constexpr nvmlReturn_t kNvmlSuccess = 0;
  using NvmlInitFn = nvmlReturn_t (*)();
  using NvmlShutdownFn = nvmlReturn_t (*)();
  using NvmlGetCountFn = nvmlReturn_t (*)(unsigned int*);
  using NvmlGetHandleFn = nvmlReturn_t (*)(unsigned int, nvmlDevice_t*);
  using NvmlMemoryInfoFn = nvmlReturn_t (*)(nvmlDevice_t, NvmlMemoryInfo*);
  using NvmlUtilizationFn = nvmlReturn_t (*)(nvmlDevice_t, NvmlUtilizationInfo*);
  using NvmlTemperatureFn = nvmlReturn_t (*)(nvmlDevice_t, unsigned int, unsigned int*);
  constexpr unsigned int kNvmlTemperatureGpu = 0;

  const auto init = reinterpret_cast<NvmlInitFn>(dlsym(lib, "nvmlInit_v2"));
  const auto shutdown = reinterpret_cast<NvmlShutdownFn>(dlsym(lib, "nvmlShutdown"));
  const auto get_count =
    reinterpret_cast<NvmlGetCountFn>(dlsym(lib, "nvmlDeviceGetCount_v2"));
  const auto get_handle =
    reinterpret_cast<NvmlGetHandleFn>(dlsym(lib, "nvmlDeviceGetHandleByIndex_v2"));
  const auto get_memory =
    reinterpret_cast<NvmlMemoryInfoFn>(dlsym(lib, "nvmlDeviceGetMemoryInfo"));
  const auto get_utilization =
    reinterpret_cast<NvmlUtilizationFn>(dlsym(lib, "nvmlDeviceGetUtilizationRates"));
  const auto get_temperature =
    reinterpret_cast<NvmlTemperatureFn>(dlsym(lib, "nvmlDeviceGetTemperature"));
  if (init == nullptr || shutdown == nullptr || get_count == nullptr || get_handle == nullptr ||
      get_memory == nullptr || get_utilization == nullptr) {
    dlclose(lib);
    return std::nullopt;
  }

  if (init() != kNvmlSuccess) {
    dlclose(lib);
    return std::nullopt;
  }

  naim::GpuTelemetrySnapshot snapshot;
  snapshot.degraded = false;
  snapshot.source = "nvml";
  unsigned int device_count = 0;
  if (get_count(&device_count) != kNvmlSuccess) {
    shutdown();
    dlclose(lib);
    return std::nullopt;
  }
  for (unsigned int index = 0; index < device_count; ++index) {
    nvmlDevice_t handle = nullptr;
    if (get_handle(index, &handle) != kNvmlSuccess || handle == nullptr) {
      continue;
    }
    NvmlMemoryInfo memory{};
    NvmlUtilizationInfo utilization{};
    if (get_memory(handle, &memory) != kNvmlSuccess ||
        get_utilization(handle, &utilization) != kNvmlSuccess) {
      continue;
    }
    naim::GpuDeviceTelemetry device;
    device.gpu_device = std::to_string(index);
    device.total_vram_mb = static_cast<int>(memory.total / (1024 * 1024));
    device.used_vram_mb = static_cast<int>(memory.used / (1024 * 1024));
    device.free_vram_mb = static_cast<int>(memory.free / (1024 * 1024));
    device.gpu_utilization_pct = static_cast<int>(utilization.gpu);
    unsigned int temperature_c = 0;
    if (get_temperature != nullptr &&
        get_temperature(handle, kNvmlTemperatureGpu, &temperature_c) == kNvmlSuccess) {
      device.temperature_c = static_cast<int>(temperature_c);
      device.temperature_available = true;
    }
    snapshot.devices.push_back(std::move(device));
  }

  shutdown();
  dlclose(lib);
  return snapshot;
#endif
}

void HostdSystemTelemetryCollector::populateGpuProcessesFromNvidiaSMI(
  GpuTelemetrySnapshot *snapshot,
  const std::vector<naim::RuntimeProcessStatus>& instance_statuses) const{

  if (snapshot == nullptr) {
    return;
  }
  std::map<int, std::string> pid_to_instance_name;
  for (const auto& status : instance_statuses) {
    if (status.engine_pid > 0) {
      pid_to_instance_name[status.engine_pid] = status.instance_name;
    }
    if (status.runtime_pid > 0) {
      pid_to_instance_name[status.runtime_pid] = status.instance_name;
    }
  }

  std::map<std::string, std::string> uuid_to_gpu_device;
  {
    const std::string output = command_support_.RunCommandCapture(
      "nvidia-smi --query-gpu=index,uuid --format=csv,noheader,nounits 2>/dev/null");
    std::istringstream input(output);
    std::string line;
    while (std::getline(input, line)) {
      const auto columns = splitCsvRow(line);
      if (columns.size() >= 2) {
        uuid_to_gpu_device[columns[1]] = columns[0];
      }
    }
  }

  const std::string output = command_support_.RunCommandCapture(
    "nvidia-smi --query-compute-apps=gpu_uuid,pid,used_gpu_memory "
    "--format=csv,noheader,nounits 2>/dev/null");
  std::istringstream input(output);
  std::string line;
  while (std::getline(input, line)) {
    const auto columns = splitCsvRow(line);
    if (columns.size() < 3) {
      continue;
    }
    const auto gpu_it = uuid_to_gpu_device.find(columns[0]);
    if (gpu_it == uuid_to_gpu_device.end()) {
      continue;
    }
    int pid = 0;
    int used_vram_mb = 0;
    try {
      pid = std::stoi(columns[1]);
      used_vram_mb = std::stoi(columns[2]);
    } catch (const std::exception&) {
      continue;
    }
    for (auto& device : snapshot->devices) {
      if (device.gpu_device != gpu_it->second) {
        continue;
      }
      naim::GpuProcessTelemetry process;
      process.pid = pid;
      process.used_vram_mb = used_vram_mb;
      const auto owner_it = pid_to_instance_name.find(pid);
      if (owner_it != pid_to_instance_name.end()) {
        process.instance_name = owner_it->second;
      }
      device.processes.push_back(std::move(process));
      break;
    }
  }
}
#endif
}  // namespace naim::hostd
