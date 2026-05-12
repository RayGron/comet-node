#pragma once

#include <string>
#include <vector>

#include "naim/state/sqlite_store.h"

namespace naim::controller {

struct MaglevWorkflowSkillDefinition {
  const char* id;
  const char* name;
  const char* description;
  const char* content;
  std::vector<std::string> match_terms;
};

class MaglevWorkflowSkillCatalog final {
 public:
  static const std::vector<MaglevWorkflowSkillDefinition>& Definitions();
  static std::vector<std::string> SkillIds();
  static void EnsureRecords(naim::ControllerStore& store);
};

}  // namespace naim::controller
