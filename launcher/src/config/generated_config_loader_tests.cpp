#include "config/generated_config_loader.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void Expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

}  // namespace

int main() {
  try {
    namespace fs = std::filesystem;
    const fs::path temp_path =
        fs::temp_directory_path() / "naim-generated-config-loader-tests.toml";
    {
      std::ofstream output(temp_path);
      output << "[hostd]\n";
      output << "node_name = \"local-hostd\"\n";
      output << "storage_root = \"/tmp/naim-storage-from-generated-config\"\n";
      output << "runtime_root = \"/tmp/naim-runtime\"\n";
      output << "state_root = \"/tmp/naim-state\"\n";
    }

    const naim::launcher::InstallLayoutResolver install_layout_resolver;
    const naim::launcher::GeneratedConfigLoader loader(install_layout_resolver);
    const auto config = loader.Load(temp_path);
    Expect(
        config.hostd.storage_root.has_value(),
        "generated hostd config should expose storage_root");
    Expect(
        config.hostd.storage_root->string() ==
            "/tmp/naim-storage-from-generated-config",
        "generated hostd config should preserve storage_root");

    std::error_code cleanup_error;
    fs::remove(temp_path, cleanup_error);
    std::cout << "generated config loader tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << "\n";
    return 1;
  }
}
