#pragma once

#include <optional>
#include <string>

#include "worker_config.h"
#include "worker_model_resolver.h"
#include "worker_signal_service.h"
#include "worker_status_service.h"

namespace comet::worker {

class WorkerEngineHost final {
 public:
  explicit WorkerEngineHost(WorkerConfig config);
  ~WorkerEngineHost();

  WorkerEngineHost(const WorkerEngineHost&) = delete;
  WorkerEngineHost& operator=(const WorkerEngineHost&) = delete;

  int Run();

 private:
  WorkerConfig config_;
  WorkerSignalService signal_service_;
  WorkerModelResolver model_resolver_;
  WorkerStatusService status_service_;
  std::string started_at_;
  std::optional<std::string> loaded_model_path_;
};

}  // namespace comet::worker
