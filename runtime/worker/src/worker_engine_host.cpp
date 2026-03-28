#include "worker_engine_host.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace comet::worker {

WorkerEngineHost::WorkerEngineHost(WorkerConfig config)
    : config_(std::move(config)),
      signal_service_(),
      model_resolver_(),
      status_service_(),
      started_at_(status_service_.CurrentTimestamp()) {}

WorkerEngineHost::~WorkerEngineHost() = default;

int WorkerEngineHost::Run() {
  signal_service_.RegisterHandlers();
  std::cout << "[comet-workerd] booting plane=" << config_.plane_name
            << " instance=" << config_.instance_name
            << " node=" << config_.node_name
            << " gpu=" << (config_.gpu_device.empty() ? "(auto)" : config_.gpu_device)
            << "\n";

  while (!signal_service_.StopRequested()) {
    try {
      const auto resolved_model_path = model_resolver_.ResolveModelPath(config_);
      if (!resolved_model_path.has_value()) {
        status_service_.MarkWaitingForModel(config_, started_at_, loaded_model_path_);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        continue;
      }
      if (!loaded_model_path_.has_value() || *loaded_model_path_ != *resolved_model_path) {
        loaded_model_path_ = *resolved_model_path;
      }

      status_service_.MarkRunning(config_, started_at_, loaded_model_path_.value_or(""));
      std::this_thread::sleep_for(std::chrono::seconds(2));
    } catch (const std::exception& error) {
      status_service_.MarkFailed(config_, started_at_, loaded_model_path_);
      std::cerr << "[comet-workerd] " << error.what() << "\n";
      std::this_thread::sleep_for(std::chrono::seconds(2));
    }
  }

  status_service_.MarkStopped(config_, started_at_, loaded_model_path_);
  return 0;
}

}  // namespace comet::worker
