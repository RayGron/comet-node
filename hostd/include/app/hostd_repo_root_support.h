#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "comet/state/models.h"

namespace comet::hostd {

class HostdRepoRootSupport final {
 public:
  std::optional<std::filesystem::path> DetectCometRepoRoot() const;
  std::optional<std::filesystem::path> ResolvePlaneOwnedPath(
      const comet::DesiredState& state,
      const std::string& relative_path,
      const std::string& artifacts_root) const;

 private:
  static std::optional<std::filesystem::path> DetectCometRepoRootNear(
      const std::filesystem::path& start);
  static std::optional<std::filesystem::path> FindRepoRootFromPath(
      std::filesystem::path current);
  static std::optional<std::filesystem::path> FindRepoRootInSiblingRepos(
      std::filesystem::path current);
  static bool LooksLikeCometRepoRoot(const std::filesystem::path& path);
  static std::string StripBundlePrefixIfPresent(const std::string& value);
};

}  // namespace comet::hostd
