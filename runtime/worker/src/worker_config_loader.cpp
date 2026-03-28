#include "worker_config_loader.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace comet::worker {

WorkerConfig WorkerConfigLoader::Load() const {
  WorkerConfig config;
  config.plane_name = GetEnvOr("COMET_PLANE_NAME", "unknown");
  config.instance_name = GetEnvOr("COMET_INSTANCE_NAME", "worker");
  config.instance_role = GetEnvOr("COMET_INSTANCE_ROLE", "worker");
  config.node_name = GetEnvOr("COMET_NODE_NAME");
  config.control_root = GetEnvOr("COMET_CONTROL_ROOT");
  config.shared_disk_path = GetEnvOr("COMET_SHARED_DISK_PATH", "/comet/shared");
  config.private_disk_path = GetEnvOr("COMET_PRIVATE_DISK_PATH", "/comet/private");
  config.status_path = GetEnvOr(
      "COMET_WORKER_RUNTIME_STATUS_PATH",
      (std::filesystem::path(config.private_disk_path) / "worker-runtime-status.json").string());
  config.model_path = GetEnvOr("COMET_WORKER_MODEL_PATH");
  config.gpu_device = GetEnvOr("COMET_GPU_DEVICE", GetEnvOr("COMET_WORKER_GPU_DEVICE"));
  config.llama_ctx_size = GetEnvIntOr("COMET_WORKER_CTX_SIZE", 2048);
  config.llama_threads = GetEnvIntOr("COMET_WORKER_THREADS", 2);
  config.llama_gpu_layers = GetEnvIntOr("COMET_LLAMA_GPU_LAYERS", 99);
  config.graceful_stop_timeout_sec =
      GetEnvIntOr("COMET_WORKER_GRACEFUL_STOP_TIMEOUT_SEC", 15);
  return config;
}

std::string WorkerConfigLoader::GetEnvOr(const char* name, const std::string& fallback) {
  const char* value = std::getenv(name);
  if (value == nullptr || std::strlen(value) == 0) {
    return fallback;
  }
  return value;
}

int WorkerConfigLoader::GetEnvIntOr(const char* name, int fallback) {
  const char* value = std::getenv(name);
  if (value == nullptr || std::strlen(value) == 0) {
    return fallback;
  }
  return std::stoi(value);
}

}  // namespace comet::worker
