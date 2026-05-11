#pragma once

#include <string>
#include <vector>

#include "naim/state/sqlite_store.h"

namespace naim::controller {

struct CodeAgentCommonSkillDefinition {
  const char* id;
  const char* name;
  const char* group_path;
  const char* description;
  const char* content;
  std::vector<std::string> match_terms;
};

inline const std::vector<CodeAgentCommonSkillDefinition>&
CodeAgentCommonSkillDefinitions() {
  static const std::vector<CodeAgentCommonSkillDefinition> definitions{
      {
          "code-agent-safe-change",
          "safe-change",
          "code-agent/planning",
          "Minimal safe change skill with explicit scope and risk framing.",
          "For fix proposals, prefer the smallest safe change and keep the answer "
          "repository-grounded. 1. First decide whether the correct answer is an "
          "operational recommendation, a config change, or a code change. 2. If "
          "the existing system already has a safe supported path, prefer using it "
          "over introducing bypasses. 3. Name the exact files or interfaces touched "
          "by the proposed change. 4. Explain why a smaller option is enough, or "
          "why anything smaller would be unsafe. 5. End with explicit risks or "
          "assumptions.",
          {"safe change", "minimal fix", "smallest fix", "safest fix",
           "минимальный безопасный фикс", "маленький патч",
           "минимальный способ", "минимальный безопасный способ",
           "безопасное изменение"},
      },
      {
          "code-agent-api-contract-guard",
          "api-contract-guard",
          "code-agent/contracts",
          "Protect API contracts when touching controller or runtime endpoints.",
          "When changing an HTTP or RPC surface: 1. Track request fields, response "
          "shape, auth requirements, status codes, and error payloads. 2. Preserve "
          "compatibility unless a breaking change is explicitly requested. 3. "
          "Update every dependent layer that relies on the contract: controller, "
          "client, UI, tests, and docs. 4. Verify both the happy path and at least "
          "one failure path. If a contract change is unavoidable, state it "
          "explicitly and minimize its blast radius.",
          {"api contract", "endpoint contract", "status code", "request shape",
           "response shape", "не ломай API контракт", "проверь эндпоинты",
           "статусы ответа"},
      },
      {
          "code-agent-root-cause-debug",
          "root-cause-debug",
          "code-agent/debugging",
          "Root cause debugging skill with explicit evidence sections.",
          "For bug and regression debugging, do not jump to a patch until the "
          "cause is verified. 1. Separate symptom, trigger, verified root cause, "
          "and remaining unknowns. 2. Ground every repo-specific claim in exact "
          "file paths or code path evidence. 3. Explain why the cited path "
          "produces the observed symptom; do not stop at a nearby symptom. 4. If "
          "evidence is incomplete, say what is still missing instead of guessing.",
          {"root cause", "корневая причина", "найди причину бага",
           "разбери регрессию", "unauthorized", "401", "forbidden",
           "auth bug", "session token", "почему curl", "почему 401"},
      },
      {
          "code-agent-deploy-path-check",
          "deploy-path-check",
          "code-agent/operations",
          "Explain the real deployment path and required live verification steps.",
          "Treat deployment as part of delivery, but distinguish guidance from "
          "execution. 1. Explain which rebuilds, image rebuilds, restarts, pulls, "
          "or redeploy steps are required. 2. Explain the real rollout path that "
          "the project uses, including hpc1 when relevant. 3. Explain how you "
          "would verify the live artifact, service, or bundle after rollout. 4. "
          "If the user is asking how deployment should be validated, do not start "
          "pull, restart, or deploy actions automatically. Execute deployment "
          "steps only when the user explicitly asks you to perform the rollout.",
          {"deploy", "deployment", "rollout", "hpc1", "live verification",
           "путь выката", "деплой", "рестарт", "live проверка"},
      },
      {
          "code-agent-repo-map",
          "repo-map",
          "code-agent/analysis",
          "Repository mapping skill with rigid grounded output.",
          "For repository mapping tasks, stay in analysis mode unless the user "
          "explicitly asks for a patch. 1. Use only repository evidence that is "
          "already supplied or loaded by the workflow. 2. List 3 to 7 relevant "
          "modules with exact file paths and one sentence per module explaining "
          "its role. 3. Describe the control flow or dependency chain in a short "
          "arrow-style sequence using exact module names. 4. Prefer concrete "
          "evidence paths over generic architecture advice. 5. If the chain is "
          "incomplete, end with a short Unknowns section instead of inventing "
          "structure.",
          {"repo map", "repository map", "карта репозитория", "карта модулей",
           "структура кода", "где менять", "какие модули", "dependency chain",
           "trace flow"},
      },
      {
          "code-agent-remote-ops",
          "remote-ops",
          "code-agent/operations",
          "Tool-aware remote operations over ssh and standard service/log commands.",
          "For remote operations tasks, infer the narrowest safe ssh-backed command "
          "that answers the question. Prefer read-only inspection like hostname, "
          "uptime, journalctl, or systemctl status before proposing any mutating "
          "remote action. Summarize the concrete remote result and leave risky "
          "follow-up steps pending approval.",
          {"remote ops", "через ssh", "подключись по ssh", "journalctl на",
           "systemctl на", "на удаленной машине"},
      },
      {
          "code-agent-plane-lifecycle-check",
          "plane-lifecycle-check",
          "code-agent/naim",
          "Verify the effect of changes across the full plane lifecycle.",
          "For changes that affect planes, verify lifecycle behavior end to end. "
          "1. Consider create, edit, start, stop, restart, reconciliation, and "
          "delete paths. 2. Check desired state, applied state, runtime artifacts, "
          "and controller bookkeeping together. 3. Track generation, "
          "applied_generation, state, assignments, and readiness signals. 4. If a "
          "change only looks correct in steady state, probe the transition states "
          "as well. Treat lifecycle regressions as first-class bugs.",
          {"plane lifecycle", "create plane", "start plane", "stop plane",
           "restart plane", "жизненный цикл плейна", "create edit start stop",
           "applied_generation"},
      },
      {
          "code-agent-refactor-rules-enforcer",
          "refactor-rules-enforcer",
          "code-agent/refactoring",
          "Apply repo refactoring rules deliberately instead of opportunistically.",
          "When refactoring, follow the repository's explicit refactoring rules "
          "rather than improvising style changes. 1. Split oversized files by "
          "ownership and responsibility. 2. Remove dead code instead of archiving "
          "it in place, unless the repository rules say otherwise. 3. Keep "
          "entrypoints thin and move reusable domain logic into clear units. 4. "
          "Avoid mixing behavior changes with structure-only refactors unless "
          "required for safety. Explain which repository rules drove the refactor "
          "when the reasoning is not obvious.",
          {"refactor", "refactoring rules", "рефакторинг по правилам",
           "split oversized files", "dead code"},
      },
      {
          "code-agent-pr-review",
          "pr-review",
          "code-agent/review",
          "Tool-aware review skill using git and gh evidence.",
          "For review tasks, gather concrete diff evidence with git and GitHub PR "
          "status with gh before summarizing. Keep the result findings-first, "
          "grounded in actual changed files and diff output. If there are no local "
          "changes, say that explicitly instead of inventing findings.",
          {"pr review", "code review", "review current changes", "сделай ревью",
           "проведи code review"},
      },
      {
          "code-agent-skill-authoring",
          "skill-authoring",
          "code-agent/skills",
          "Write reusable skills with narrow scope, clear triggers, and testable outcomes.",
          "When creating or editing a skill, optimize for reuse and clarity. 1. "
          "Give the skill one main purpose, not a broad role description. 2. Make "
          "the trigger obvious from the name and description. 3. Keep instructions "
          "concrete, ordered, and testable. 4. Avoid overlap with nearby skills "
          "unless the distinction is explicit. A good skill should be easy to "
          "select, easy to verify, and hard to misuse.",
          {"write skill", "skill authoring", "create skill", "написать skill",
           "триггеры", "узкий scope"},
      },
      {
          "code-agent-state-schema-guard",
          "state-schema-guard",
          "code-agent/contracts",
          "Protect desired-state, projectors, validators, renderers, and store changes.",
          "When changing state or persistence contracts, treat the whole pipeline "
          "as one unit. 1. Check models, JSON codecs, validators, projectors, "
          "renderers, repository/store code, and tests together. 2. Preserve "
          "backward compatibility unless the task explicitly requires a contract "
          "break. 3. Verify round-trip behavior for the changed state. 4. Look for "
          "missing migration or partial-update paths, not just compile errors. Do "
          "not change desired-state or persisted schema in only one layer.",
          {"desired-state", "state schema", "projector", "validator",
           "renderer", "store", "desired-state схема",
           "projector validator renderer store"},
      },
      {
          "code-agent-build-and-test",
          "build-and-test",
          "code-agent/verification",
          "Tool-aware build and test orchestration with local build and test commands.",
          "For build and test tasks, prefer local build/test tools over prose. "
          "Produce the smallest executable sequence needed to configure, build, "
          "and run tests in the current repository. Prefer existing build "
          "directories when they already exist. Summarize concrete build and test "
          "outcomes, including failing command names or the fact that all checks "
          "passed.",
          {"build and test", "собери и прогони тесты", "запусти тесты",
           "run tests", "build this repo"},
      },
      {
          "code-agent-test-first-fix",
          "test-first-fix",
          "code-agent/verification",
          "Explain a test-first bug-fix approach without starting execution implicitly.",
          "When the user asks about fixing a bug or regression, prefer a "
          "test-first explanation. 1. Explain that you would add or identify the "
          "narrowest regression test first. 2. Explain that the fix should come "
          "after the regression is captured. 3. Explain that the affected tests "
          "should be rerun after the fix. 4. If the user is asking for guidance or "
          "approach only, do not start editing files, running tests, or applying "
          "changes on your own. Switch from explanation to execution only when the "
          "user explicitly asks you to perform the fix.",
          {"test first", "regression test", "сначала тест", "потом фикс"},
      },
      {
          "code-agent-ui-runtime-parity",
          "ui-runtime-parity",
          "code-agent/ui",
          "Verify that a UI feature exists in source, build output, and live bundle.",
          "For UI-facing work, verify all three layers. 1. Confirm the source code "
          "contains the intended feature and event path. 2. Build the UI and "
          "verify the generated asset includes the expected behavior or strings. "
          "3. If deployed, verify the live bundle or live page actually serves the "
          "updated feature. 4. Cross-check the UI against the backend API it "
          "depends on. Do not assume that code presence alone means the user-facing "
          "feature is available.",
          {"ui runtime parity", "live bundle", "source build live",
           "проверь UI в source", "build и live bundle"},
      },
      {
          "code-agent-github-workflow",
          "github-workflow",
          "code-agent/github",
          "Tool-aware GitHub workflow skill for issues and PRs via gh and git.",
          "For GitHub workflow tasks, prefer gh over prose. Infer the minimal "
          "sequence of repository-aware tool steps needed to inspect or mutate "
          "issue/PR state. When the user asks to create an issue, produce the "
          "smallest valid issue title/body, target the current repository unless "
          "another repo is named explicitly, and avoid extra commentary. Use git "
          "only when repository context or branch state matters. Summarize the "
          "resulting GitHub state with issue or PR identifiers.",
          {"create issue", "github issue", "open issue", "создай issue",
           "issue в github", "gh workflow"},
      },
      {
          "code-agent-issue-closeout",
          "issue-closeout",
          "code-agent/github",
          "Explain good issue closeout and perform it only when explicitly requested.",
          "Treat issue closeout as a delivery checklist, but do not execute it "
          "implicitly. 1. Explain that relevant tests and live checks should be "
          "rerun first. 2. Explain that the implemented scope and verification "
          "should be summarized clearly. 3. Explain that linked issues, checklists, "
          "labels, milestones, and closeout notes should be updated only when the "
          "scope is truly complete. 4. If the user asks for guidance, do not mutate "
          "issue state on your own. Close or update issues only when the user "
          "explicitly asks you to perform the closeout.",
          {"issue closeout", "close issue", "закрытие issue", "completed",
           "closeout после проверки"},
      },
  };
  return definitions;
}

inline std::vector<std::string> CodeAgentCommonSkillIds() {
  std::vector<std::string> ids;
  for (const auto& definition : CodeAgentCommonSkillDefinitions()) {
    ids.push_back(definition.id);
  }
  return ids;
}

inline void EnsureCodeAgentCommonSkillRecords(naim::ControllerStore& store) {
  for (const auto& definition : CodeAgentCommonSkillDefinitions()) {
    store.UpsertSkillsFactorySkill(naim::SkillsFactorySkillRecord{
        definition.id,
        definition.name,
        definition.group_path,
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
