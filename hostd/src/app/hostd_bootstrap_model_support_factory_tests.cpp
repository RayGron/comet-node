#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "app/hostd_bootstrap_model_support_factory.h"

namespace {

void Expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

comet::DesiredState BuildBootstrapState(
    const std::string& shared_root,
    const std::string& source_model_path) {
  comet::DesiredState state;
  state.plane_name = "plane-a";
  state.control_root = "/workspace/shared/control/plane-a";
  comet::BootstrapModelSpec bootstrap_model;
  bootstrap_model.model_id = "model-a";
  bootstrap_model.materialization_mode = "reference";
  bootstrap_model.local_path = source_model_path;
  state.bootstrap_model = bootstrap_model;

  comet::DiskSpec disk;
  disk.name = "plane-a-shared";
  disk.plane_name = "plane-a";
  disk.node_name = "node-a";
  disk.kind = comet::DiskKind::PlaneShared;
  disk.host_path = shared_root;
  disk.container_path = "/workspace/shared";
  state.disks.push_back(disk);

  comet::InstanceSpec infer;
  infer.name = "infer-a";
  infer.role = comet::InstanceRole::Infer;
  infer.node_name = "node-a";
  state.instances.push_back(infer);
  return state;
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    throw std::runtime_error("failed to open file: " + path.string());
  }
  return std::string(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
  try {
    namespace fs = std::filesystem;

    const comet::hostd::HostdDesiredStatePathSupport path_support;
    const comet::hostd::HostdCommandSupport command_support;
    const comet::hostd::HostdFileSupport file_support;
    const comet::hostd::HostdReportingSupport reporting_support;
    const comet::hostd::HostdBootstrapModelSupportFactory factory(
        path_support,
        command_support,
        file_support,
        reporting_support);

    const fs::path temp_root =
        fs::temp_directory_path() / "comet-hostd-bootstrap-model-support-factory-tests";
    std::error_code cleanup_error;
    fs::remove_all(temp_root, cleanup_error);
    fs::create_directories(temp_root / "shared");
    fs::create_directories(temp_root / "models");

    const fs::path source_model_path = temp_root / "models" / "model.gguf";
    {
      std::ofstream output(source_model_path, std::ios::binary | std::ios::trunc);
      output << "gguf";
    }

    {
      auto state = BuildBootstrapState((temp_root / "shared").string(), source_model_path.string());
      auto support = factory.Create();
      support.BootstrapPlaneModelIfNeeded(state, "node-a", nullptr, std::nullopt);

      const fs::path active_model_path =
          temp_root / "shared" / "control" / "plane-a" / "active-model.json";
      Expect(fs::exists(active_model_path), "factory-created support should write active-model");
      const auto contents = ReadFile(active_model_path);
      Expect(
          contents.find(source_model_path.string()) != std::string::npos,
          "active-model should reference configured source path");
      Expect(
          contents.find("\"model_id\": \"model-a\"") != std::string::npos,
          "active-model should contain model id");
    }

    {
      auto state = BuildBootstrapState((temp_root / "shared").string(), source_model_path.string());
      state.bootstrap_model.reset();
      auto support = factory.Create();
      support.BootstrapPlaneModelIfNeeded(state, "node-a", nullptr, std::nullopt);

      const fs::path active_model_path =
          temp_root / "shared" / "control" / "plane-a" / "active-model.json";
      Expect(
          !fs::exists(active_model_path),
          "factory-created support should remove active-model when bootstrap config is absent");
    }

    fs::remove_all(temp_root, cleanup_error);

    std::cout << "ok: hostd-bootstrap-model-support-factory-reference-mode\n";
    std::cout << "ok: hostd-bootstrap-model-support-factory-remove-active-model\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }
}
