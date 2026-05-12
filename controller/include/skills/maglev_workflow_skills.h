#pragma once

#include <string>
#include <vector>

#include "naim/state/models.h"
#include "naim/state/sqlite_store.h"

namespace naim::controller {

struct MaglevWorkflowSkillDefinition {
  const char* id;
  const char* name;
  const char* description;
  const char* content;
  std::vector<std::string> match_terms;
};

inline const std::vector<MaglevWorkflowSkillDefinition>&
MaglevWorkflowSkillDefinitions() {
  static const std::vector<MaglevWorkflowSkillDefinition> definitions{
      {
          "maglev-client-workflow",
          "Maglev client workflow",
          "Guide Maglev client users through dialog-first local workflow.",
          "Use this skill when the user asks how to work inside Maglev, which slash "
          "commands are available, or how to avoid leaving the dialog for normal "
          "actions. Treat the Maglev client as the user's primary workspace. Prefer "
          "dialog commands such as /auth, /skills, /skill-add, /skill-delete, "
          "/skill-use, /skill-clear, /client-sync, /kv, and /remember. Explain the "
          "shortest local-first path and avoid sending the user to controller UI or "
          "plane internals unless they ask for administration or recovery steps. "
          "For skill import, /skill-add requires a relative or absolute path to a "
          "skill JSON file; do not describe a pasted-JSON prompt flow unless the "
          "client explicitly implements one.",
          {
              "maglev workflow",
              "maglev client",
              "dialog command",
              "slash command",
              "inside dialog",
              "/auth",
              "/skills",
              "/skill-add",
              "/skill-delete",
              "/client-sync",
              "workflow maglev",
              "команды maglev",
              "внутри диалога",
              "слеш команды",
              "клиент maglev",
              "маглев workflow",
          },
      },
      {
          "maglev-client-knowledge-vault-local-first",
          "Maglev local Knowledge Vault",
          "Use Maglev's client-owned Knowledge Vault copy before remote plane APIs.",
          "Use this skill when the user asks to remember, recall, search, cite, "
          "delete, clean up, or reason over Knowledge Vault data from Maglev. The "
          "normal request-time path is local-first: use the client-owned SQLite "
          "Knowledge Vault projection and cite local knowledge_id or block_id when "
          "available. Plane-owned Knowledge Vault APIs are bootstrap, sync, and "
          "fallback channels. Use /remember for local writes, /kv status/search/"
          "graph/delete/cleanup for local inspection and maintenance, and "
          "/client-sync push or pull when remote reconciliation is needed.",
          {
              "maglev knowledge vault",
              "local knowledge vault",
              "client-owned knowledge",
              "client owned knowledge",
              "local-first knowledge",
              "/remember",
              "/kv",
              "kv search",
              "kv status",
              "knowledge citations",
              "local sqlite",
              "локальный knowledge vault",
              "локальная память",
              "запомни",
              "найди в памяти",
              "цитаты knowledge",
          },
      },
      {
          "maglev-client-skills-factory-workflow",
          "Maglev local skills workflow",
          "Manage Maglev skills through the client-owned runtime and dialog commands.",
          "Use this skill when the user asks to list, add, delete, select, clear, or "
          "inspect skills from Maglev. Prefer the client-owned skills copy during "
          "normal work. Use /skills to inspect local skills, /skill-add <path> to "
          "import a skill JSON file, /skill-delete <name-or-id> to remove one, "
          "/skill-use to pin a skill for the dialog, and /skill-clear to return to "
          "automatic skill resolution. /skill-add accepts a relative or absolute "
          "file path, not pasted JSON content. Treat the controller SkillsFactory "
          "as the canonical bootstrap and sync source, not as the request-time selector.",
          {
              "maglev skills",
              "local skills",
              "client-owned skills",
              "skill json",
              "add skill",
              "delete skill",
              "remove skill",
              "/skill-add",
              "/skill-delete",
              "/skill-use",
              "/skill-clear",
              "/skills-session",
              "добавить скилл",
              "удалить скилл",
              "локальные скиллы",
              "json скилла",
          },
      },
      {
          "maglev-client-sync-and-conflicts",
          "Maglev sync and conflicts",
          "Explain Maglev client sync, offline outbox, and conflict review behavior.",
          "Use this skill when the user asks about Maglev bootstrap, sync status, "
          "offline operation, outbox, conflicts, or why local state differs from the "
          "plane. Maglev should use local state immediately after activation. Plane "
          "sync pulls authorized Skills and Knowledge Vault data, pushes queued local "
          "writes when reachable, and keeps conflicts for review rather than silently "
          "overwriting remote changes. Recommend /client-sync status before diagnosis "
          "and /client-sync pull or push only when the user wants reconciliation.",
          {
              "maglev sync",
              "client sync",
              "sync conflict",
              "offline outbox",
              "bootstrap maglev",
              "client runtime status",
              "/client-sync status",
              "/client-sync pull",
              "/client-sync push",
              "conflict review",
              "outbox",
              "синхронизация maglev",
              "конфликт синхронизации",
              "оффлайн очередь",
              "статус синхронизации",
          },
      },
  };
  return definitions;
}

inline std::vector<std::string> MaglevWorkflowSkillIds() {
  std::vector<std::string> ids;
  for (const auto& definition : MaglevWorkflowSkillDefinitions()) {
    ids.push_back(definition.id);
  }
  return ids;
}

inline void EnsureMaglevWorkflowSkillRecords(naim::ControllerStore& store) {
  for (const auto& definition : MaglevWorkflowSkillDefinitions()) {
    store.UpsertSkillsFactorySkill(naim::SkillsFactorySkillRecord{
        definition.id,
        definition.name,
        "maglev",
        definition.description,
        definition.content,
        definition.match_terms,
        false,
        "",
        "",
    });
  }
}

}  // namespace naim::controller
