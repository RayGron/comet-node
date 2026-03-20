#include "comet/import_bundle.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace comet {

namespace {

using json = nlohmann::json;

json ReadJsonFile(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    throw std::runtime_error("failed to open json file '" + path.string() + "'");
  }

  json value;
  input >> value;
  return value;
}

std::string RequiredString(const json& value, const char* key, const std::string& context) {
  if (!value.contains(key) || !value.at(key).is_string()) {
    throw std::runtime_error(context + " is missing required string field '" + key + "'");
  }
  return value.at(key).get<std::string>();
}

int OptionalInt(const json& value, const char* key, int default_value) {
  if (!value.contains(key)) {
    return default_value;
  }
  if (!value.at(key).is_number_integer()) {
    throw std::runtime_error(std::string("field '") + key + "' must be an integer");
  }
  return value.at(key).get<int>();
}

double OptionalDouble(const json& value, const char* key, double default_value) {
  if (!value.contains(key)) {
    return default_value;
  }
  if (!value.at(key).is_number()) {
    throw std::runtime_error(std::string("field '") + key + "' must be numeric");
  }
  return value.at(key).get<double>();
}

std::string OptionalString(const json& value, const char* key, const std::string& default_value) {
  if (!value.contains(key)) {
    return default_value;
  }
  if (!value.at(key).is_string()) {
    throw std::runtime_error(std::string("field '") + key + "' must be a string");
  }
  return value.at(key).get<std::string>();
}

std::optional<std::string> OptionalStringOpt(const json& value, const char* key) {
  if (!value.contains(key) || value.at(key).is_null()) {
    return std::nullopt;
  }
  if (!value.at(key).is_string()) {
    throw std::runtime_error(std::string("field '") + key + "' must be a string");
  }
  return value.at(key).get<std::string>();
}

DiskSpec MakeDisk(
    std::string name,
    DiskKind kind,
    std::string plane_name,
    std::string owner_name,
    std::string node_name,
    std::string host_path,
    std::string container_path,
    int size_gb) {
  DiskSpec disk;
  disk.name = std::move(name);
  disk.kind = kind;
  disk.plane_name = std::move(plane_name);
  disk.owner_name = std::move(owner_name);
  disk.node_name = std::move(node_name);
  disk.host_path = std::move(host_path);
  disk.container_path = std::move(container_path);
  disk.size_gb = size_gb;
  return disk;
}

void ValidateNodeExists(
    const std::map<std::string, NodeInventory>& nodes_by_name,
    const std::string& node_name,
    const std::string& context) {
  if (nodes_by_name.find(node_name) == nodes_by_name.end()) {
    throw std::runtime_error(context + " references unknown node '" + node_name + "'");
  }
}

void ValidateGpuExists(
    const NodeInventory& node,
    const std::optional<std::string>& gpu_device,
    const std::string& context) {
  if (!gpu_device.has_value()) {
    return;
  }
  const auto it =
      std::find(node.gpu_devices.begin(), node.gpu_devices.end(), *gpu_device);
  if (it == node.gpu_devices.end()) {
    throw std::runtime_error(
        context + " references missing gpu '" + *gpu_device + "' on node '" + node.name + "'");
  }
}

std::vector<NodeInventory> ParseNodes(const json& plane_json) {
  std::vector<NodeInventory> nodes;
  std::set<std::string> node_names;

  if (!plane_json.contains("nodes")) {
    nodes = {
        NodeInventory{"node-a", "linux", {"0", "1"}},
        NodeInventory{"node-b", "linux", {"0"}},
    };
    return nodes;
  }

  if (!plane_json.at("nodes").is_array()) {
    throw std::runtime_error("plane.json field 'nodes' must be an array");
  }

  for (const auto& node_json : plane_json.at("nodes")) {
    NodeInventory node;
    node.name = RequiredString(node_json, "name", "plane node");
    node.platform = OptionalString(node_json, "platform", "linux");
    if (!node_names.insert(node.name).second) {
      throw std::runtime_error("plane.json contains duplicate node '" + node.name + "'");
    }

    if (node_json.contains("gpus")) {
      if (!node_json.at("gpus").is_array()) {
        throw std::runtime_error("plane node field 'gpus' must be an array");
      }
      for (const auto& gpu_json : node_json.at("gpus")) {
        if (!gpu_json.is_string()) {
          throw std::runtime_error("plane node gpu id must be a string");
        }
        node.gpu_devices.push_back(gpu_json.get<std::string>());
      }
    }

    nodes.push_back(std::move(node));
  }

  if (nodes.empty()) {
    throw std::runtime_error("plane.json must define at least one node");
  }

  return nodes;
}

std::optional<json> OptionalObject(const json& value, const char* key) {
  if (!value.contains(key) || value.at(key).is_null()) {
    return std::nullopt;
  }
  if (!value.at(key).is_object()) {
    throw std::runtime_error(std::string("field '") + key + "' must be an object");
  }
  return json(value.at(key));
}

}  // namespace

DesiredState ImportPlaneBundle(const std::string& bundle_dir) {
  const std::filesystem::path bundle_path(bundle_dir);
  const json plane_json = ReadJsonFile(bundle_path / "plane.json");
  const json infer_json = ReadJsonFile(bundle_path / "infer.json");
  const std::filesystem::path workers_dir = bundle_path / "workers";

  if (!std::filesystem::exists(workers_dir) || !std::filesystem::is_directory(workers_dir)) {
    throw std::runtime_error("bundle is missing workers directory at '" + workers_dir.string() + "'");
  }

  DesiredState state;
  state.plane_name = RequiredString(plane_json, "name", "plane.json");
  state.plane_shared_disk_name = "plane-" + state.plane_name + "-shared";
  state.control_root = OptionalString(
      plane_json,
      "control_root",
      "/comet/shared/control/" + state.plane_name);
  const int shared_disk_gb = OptionalInt(plane_json, "shared_disk_gb", 200);

  if (plane_json.contains("runtime") &&
      !plane_json.at("runtime").is_null() &&
      !plane_json.at("runtime").is_object() &&
      !plane_json.at("runtime").is_string()) {
    throw std::runtime_error("field 'runtime' must be an object or a string");
  }

  if (plane_json.contains("runtime") && plane_json.at("runtime").is_object()) {
    const auto runtime = plane_json.at("runtime");
    state.inference.primary_infer_node =
        OptionalString(runtime, "primary_infer_node", state.inference.primary_infer_node);
    state.inference.net_if = OptionalString(runtime, "net_if", state.inference.net_if);
    state.inference.models_root =
        OptionalString(runtime, "models_root", state.inference.models_root);
    state.inference.gguf_cache_dir =
        OptionalString(runtime, "gguf_cache_dir", state.inference.gguf_cache_dir);
    state.inference.infer_log_dir =
        OptionalString(runtime, "infer_log_dir", state.inference.infer_log_dir);
    state.inference.llama_port =
        OptionalInt(runtime, "llama_port", state.inference.llama_port);
    state.inference.llama_ctx_size =
        OptionalInt(runtime, "llama_ctx_size", state.inference.llama_ctx_size);
    state.inference.llama_threads =
        OptionalInt(runtime, "llama_threads", state.inference.llama_threads);
    state.inference.llama_gpu_layers =
        OptionalInt(runtime, "llama_gpu_layers", state.inference.llama_gpu_layers);
    state.inference.inference_healthcheck_retries = OptionalInt(
        runtime,
        "inference_healthcheck_retries",
        state.inference.inference_healthcheck_retries);
    state.inference.inference_healthcheck_interval_sec = OptionalInt(
        runtime,
        "inference_healthcheck_interval_sec",
        state.inference.inference_healthcheck_interval_sec);
  }

  if (const auto gateway = OptionalObject(plane_json, "gateway")) {
    state.gateway.listen_host =
        OptionalString(*gateway, "listen_host", state.gateway.listen_host);
    state.gateway.listen_port =
        OptionalInt(*gateway, "listen_port", state.gateway.listen_port);
    state.gateway.server_name =
        OptionalString(*gateway, "server_name", state.gateway.server_name);
  }

  state.nodes = ParseNodes(plane_json);
  std::map<std::string, NodeInventory> nodes_by_name;
  for (const auto& node : state.nodes) {
    nodes_by_name[node.name] = node;
  }

  for (const auto& node : state.nodes) {
    state.disks.push_back(MakeDisk(
        state.plane_shared_disk_name,
        DiskKind::PlaneShared,
        state.plane_name,
        state.plane_name,
        node.name,
        "/var/lib/comet/disks/planes/" + state.plane_name + "/shared",
        "/comet/shared",
        shared_disk_gb));
  }

  const std::string infer_name = RequiredString(infer_json, "name", "infer.json");
  const std::string infer_node_name =
      OptionalString(infer_json, "node", state.nodes.front().name);
  ValidateNodeExists(nodes_by_name, infer_node_name, "infer.json");
  if (state.inference.primary_infer_node.empty()) {
    state.inference.primary_infer_node = infer_node_name;
  }

  InstanceSpec infer;
  infer.name = infer_name;
  infer.role = InstanceRole::Infer;
  infer.plane_name = state.plane_name;
  infer.node_name = infer_node_name;
  infer.image = RequiredString(infer_json, "image", "infer.json");
  infer.command = "/runtime/infer/entrypoint.sh";
  infer.private_disk_name = infer.name + "-private";
  infer.shared_disk_name = state.plane_shared_disk_name;
  infer.private_disk_size_gb = OptionalInt(infer_json, "private_disk_gb", 80);
  infer.environment = {
      {"COMET_PLANE_NAME", state.plane_name},
      {"COMET_INSTANCE_NAME", infer.name},
      {"COMET_INSTANCE_ROLE", "infer"},
      {"COMET_INFER_BOOT_MODE", "launch-runtime"},
      {"COMET_INFER_RUNTIME_BACKEND", "auto"},
      {"COMET_CONTROLLER_URL", "http://controller.internal:8080"},
      {"COMET_CONTROL_ROOT", state.control_root},
      {"COMET_INFER_RUNTIME_CONFIG", state.control_root + "/infer-runtime.json"},
      {"COMET_INFERENCE_PORT", std::to_string(state.inference.llama_port)},
      {"COMET_GATEWAY_PORT", std::to_string(state.gateway.listen_port)},
      {"COMET_SHARED_DISK_PATH", "/comet/shared"},
      {"COMET_PRIVATE_DISK_PATH", "/comet/private"},
  };
  infer.labels = {
      {"comet.plane", state.plane_name},
      {"comet.role", "infer"},
      {"comet.node", infer.node_name},
  };

  state.disks.push_back(MakeDisk(
      infer.private_disk_name,
      DiskKind::InferPrivate,
      state.plane_name,
      infer.name,
      infer.node_name,
      "/var/lib/comet/disks/instances/" + infer.name + "/private",
      "/comet/private",
      infer.private_disk_size_gb));
  state.instances.push_back(infer);

  std::vector<std::filesystem::path> worker_files;
  for (const auto& entry : std::filesystem::directory_iterator(workers_dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") {
      worker_files.push_back(entry.path());
    }
  }
  std::sort(worker_files.begin(), worker_files.end());
  if (worker_files.empty()) {
    throw std::runtime_error("bundle must contain at least one worker json file");
  }

  std::size_t worker_index = 0;
  std::set<std::string> instance_names;
  if (!instance_names.insert(infer.name).second) {
    throw std::runtime_error("duplicate instance name '" + infer.name + "'");
  }
  for (const auto& worker_file : worker_files) {
    const json worker_json = ReadJsonFile(worker_file);
    InstanceSpec worker;
    worker.name = RequiredString(worker_json, "name", worker_file.string());
    if (!instance_names.insert(worker.name).second) {
      throw std::runtime_error("duplicate instance name '" + worker.name + "'");
    }
    worker.role = InstanceRole::Worker;
    worker.plane_name = state.plane_name;
    worker.node_name = OptionalString(
        worker_json,
        "node",
        state.nodes[worker_index % state.nodes.size()].name);
    ValidateNodeExists(nodes_by_name, worker.node_name, worker_file.string());
    const NodeInventory& node = nodes_by_name.at(worker.node_name);

    worker.image = RequiredString(worker_json, "image", worker_file.string());
    worker.command = "/runtime/worker/entrypoint.sh";
    worker.private_disk_name = worker.name + "-private";
    worker.shared_disk_name = state.plane_shared_disk_name;
    worker.private_disk_size_gb = OptionalInt(worker_json, "private_disk_gb", 40);
    worker.gpu_fraction = OptionalDouble(worker_json, "gpu_fraction", 1.0);
    worker.gpu_device = OptionalStringOpt(worker_json, "gpu_device");
    ValidateGpuExists(node, worker.gpu_device, worker_file.string());

    if (worker.node_name == infer.node_name) {
      worker.depends_on.push_back(infer.name);
    }

    worker.environment = {
        {"COMET_PLANE_NAME", state.plane_name},
        {"COMET_INSTANCE_NAME", worker.name},
        {"COMET_INSTANCE_ROLE", "worker"},
        {"COMET_CONTROL_ROOT", state.control_root},
        {"COMET_SHARED_DISK_PATH", "/comet/shared"},
        {"COMET_PRIVATE_DISK_PATH", "/comet/private"},
    };
    worker.labels = {
        {"comet.plane", state.plane_name},
        {"comet.role", "worker"},
        {"comet.node", worker.node_name},
    };

    state.disks.push_back(MakeDisk(
        worker.private_disk_name,
        DiskKind::WorkerPrivate,
        state.plane_name,
        worker.name,
        worker.node_name,
        "/var/lib/comet/disks/instances/" + worker.name + "/private",
        "/comet/private",
        worker.private_disk_size_gb));
    state.instances.push_back(worker);
    state.runtime_gpu_nodes.push_back(
        RuntimeGpuNode{
            worker.name,
            worker.node_name,
            worker.gpu_device.value_or(""),
            worker.gpu_fraction,
            true,
        });
    ++worker_index;
  }

  return state;
}

}  // namespace comet
