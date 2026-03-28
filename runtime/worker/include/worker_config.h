#pragma once

#include <string>

namespace comet::worker {

struct WorkerConfig {
  std::string plane_name;
  std::string instance_name;
  std::string instance_role = "worker";
  std::string node_name;
  std::string control_root;
  std::string shared_disk_path;
  std::string private_disk_path;
  std::string status_path;
  std::string model_path;
  std::string gpu_device;
  int llama_ctx_size = 2048;
  int llama_threads = 2;
  int llama_gpu_layers = 99;
  int graceful_stop_timeout_sec = 15;
};

}  // namespace comet::worker
