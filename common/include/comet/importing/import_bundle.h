#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "comet/state/models.h"

namespace comet {

class BundleReader {
 public:
  nlohmann::json ReadJsonFile(const std::filesystem::path& path) const;
  std::vector<std::filesystem::path> ListWorkerFiles(
      const std::filesystem::path& workers_dir) const;
};

class AutoPlacementResolver {
 public:
  DesiredState Resolve(DesiredState state) const;
};

class DesiredStateImporter {
 public:
  DesiredState Import(const std::string& bundle_dir) const;
};

DesiredState ImportPlaneBundle(const std::string& bundle_dir);

}  // namespace comet
