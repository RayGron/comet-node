#include "worker_config_loader.h"
#include "worker_engine_host.h"

#include <exception>
#include <iostream>

int main() {
  try {
    comet::worker::WorkerConfigLoader config_loader;
    comet::worker::WorkerEngineHost engine_host(config_loader.Load());
    return engine_host.Run();
  } catch (const std::exception& error) {
    std::cerr << "comet-workerd: " << error.what() << "\n";
    return 1;
  }
}
