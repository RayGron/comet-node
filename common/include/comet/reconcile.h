#pragma once

#include <optional>
#include <string>
#include <vector>

#include "comet/models.h"

namespace comet {

enum class ChangeAction {
  Create,
  Update,
  Delete,
};

struct ResourceChange {
  ChangeAction action;
  std::string resource_type;
  std::string resource_id;
  std::string details;
};

struct ReconcilePlan {
  std::vector<std::string> notes;
  std::vector<std::string> warnings;
  std::vector<ResourceChange> changes;
};

ReconcilePlan BuildReconcilePlan(
    const std::optional<DesiredState>& current_state,
    const DesiredState& desired_state);

std::string RenderReconcilePlan(const ReconcilePlan& plan);
std::string ToString(ChangeAction action);

}  // namespace comet
