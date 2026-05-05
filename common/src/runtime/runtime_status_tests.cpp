#include "naim/runtime/runtime_status.h"

#include <chrono>
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

std::filesystem::path TempPath(const std::string& name) {
  const auto suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         (name + "-" + std::to_string(suffix) + ".json");
}

void WriteText(const std::filesystem::path& path, const std::string& text) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    throw std::runtime_error("failed to open temp file");
  }
  output << text;
}

void EmptyRuntimeStatusFilesAreTreatedAsMissing() {
  const auto path = TempPath("naim-runtime-status-empty-test");
  WriteText(path, "");
  const auto status = naim::LoadRuntimeStatusJson(path.string());
  Expect(!status.has_value(), "empty runtime status should be treated as missing");
  std::filesystem::remove(path);
  std::cout << "ok: empty-runtime-status-is-missing\n";
}

void PartialRuntimeStatusFilesAreTreatedAsMissing() {
  const auto path = TempPath("naim-runtime-status-partial-test");
  WriteText(path, "{");
  const auto status = naim::LoadRuntimeStatusJson(path.string());
  Expect(!status.has_value(), "partial runtime status should be treated as missing");
  std::filesystem::remove(path);
  std::cout << "ok: partial-runtime-status-is-missing\n";
}

void ValidRuntimeStatusFilesStillLoad() {
  const auto path = TempPath("naim-runtime-status-valid-test");
  naim::RuntimeStatus source;
  source.ready = true;
  source.launch_ready = true;
  source.inference_ready = true;
  source.runtime_phase = "running";
  naim::SaveRuntimeStatusJson(source, path.string());
  const auto status = naim::LoadRuntimeStatusJson(path.string());
  Expect(status.has_value(), "valid runtime status should load");
  Expect(status->ready, "valid runtime status should preserve ready flag");
  Expect(status->runtime_phase == "running", "valid runtime status should preserve phase");
  std::filesystem::remove(path);
  std::cout << "ok: valid-runtime-status-loads\n";
}

}  // namespace

int main() {
  try {
    EmptyRuntimeStatusFilesAreTreatedAsMissing();
    PartialRuntimeStatusFilesAreTreatedAsMissing();
    ValidRuntimeStatusFilesStillLoad();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "runtime_status_tests failed: " << error.what() << "\n";
    return 1;
  }
}
