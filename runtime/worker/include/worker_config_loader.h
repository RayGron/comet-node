#pragma once

#include "worker_config.h"

namespace comet::worker {

class WorkerConfigLoader final {
 public:
  WorkerConfigLoader() = default;

  WorkerConfig Load() const;

 private:
  static std::string GetEnvOr(const char* name, const std::string& fallback = "");
  static int GetEnvIntOr(const char* name, int fallback);
};

}  // namespace comet::worker
