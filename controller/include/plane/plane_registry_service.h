#pragma once

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "comet/state/models.h"
#include "comet/state/sqlite_store.h"
#include "plane/plane_lifecycle_support.h"
#include "plane/plane_registry_query_support.h"

namespace comet::controller {

class PlaneRegistryService {
 public:
  PlaneRegistryService(
      std::shared_ptr<const PlaneLifecycleSupport> lifecycle_support,
      std::shared_ptr<const PlaneRegistryQuerySupport> query_support);

  nlohmann::json BuildPlanesPayload(const std::string& db_path) const;

 private:
  std::shared_ptr<const PlaneLifecycleSupport> lifecycle_support_;
  std::shared_ptr<const PlaneRegistryQuerySupport> query_support_;
};

}  // namespace comet::controller
