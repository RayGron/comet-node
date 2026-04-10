#pragma once

#include <string_view>

#include "comet/runtime/model_adapter.h"
#include "interaction/interaction_service.h"

namespace comet::controller {

class InteractionModelIdentityBuilder final {
 public:
  comet::runtime::ModelIdentity BuildRuntimePreferred(
      const PlaneInteractionResolution& resolution) const;

  comet::runtime::ModelIdentity BuildStatusPreferred(
      const PlaneInteractionResolution& resolution) const;

 private:
  std::string ReadJsonStringOrEmpty(
      const nlohmann::json& payload,
      std::string_view key) const;
};

}  // namespace comet::controller
