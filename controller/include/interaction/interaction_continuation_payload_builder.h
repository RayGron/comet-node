#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "interaction/interaction_types.h"

namespace comet::controller {

class InteractionContinuationPayloadBuilder final {
 public:
  nlohmann::json Build(
      const nlohmann::json& original_payload,
      const std::string& accumulated_text,
      const InteractionCompletionPolicy& policy,
      bool natural_stop_without_marker,
      int total_completion_tokens) const;
};

}  // namespace comet::controller
