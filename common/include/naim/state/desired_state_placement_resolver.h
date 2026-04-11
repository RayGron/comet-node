#pragma once

#include <optional>
#include <string>

#include "naim/state/models.h"

namespace naim {

class DesiredStatePlacementResolver final {
 public:
  explicit DesiredStatePlacementResolver(const DesiredState& state);

  bool HasPrimaryNode() const;
  std::optional<std::string> PrimaryNodeName() const;
  std::string DefaultNodeName() const;
  bool ShouldEmitTopology() const;

 private:
  bool IsDefaultSingleNodeTopology() const;
  std::optional<std::string> ResolvePlacementTargetAlias() const;

  const DesiredState& state_;
};

}  // namespace naim
