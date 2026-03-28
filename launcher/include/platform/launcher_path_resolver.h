#pragma once

#include <filesystem>
#include <string>

namespace comet::launcher {

class LauncherPathResolver {
 public:
  std::filesystem::path ResolveSelfPath(const char* argv0) const;
  std::filesystem::path ResolveSiblingBinary(
      const std::filesystem::path& self_path,
      const std::string& binary_name) const;
};

}  // namespace comet::launcher
