#pragma once

#include <optional>
#include <string>
#include <vector>

#include "comet/models.h"

namespace comet {

std::vector<NodeComposePlan> BuildNodeComposePlans(const DesiredState& state);
std::optional<NodeComposePlan> FindNodeComposePlan(
    const DesiredState& state,
    const std::string& node_name);

}  // namespace comet
