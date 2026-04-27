#!/usr/bin/env bash
set -euo pipefail

dry_run="no"
if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  cat <<'EOF'
Usage:
  configure-main-ruleset.sh [--dry-run] [owner/repo]

Adds the NAIM PR Gate summary job as a required status check on the main
repository ruleset. Run this only after naim-pr-ci.yml has landed on main.
EOF
  exit 0
fi

if [[ "${1:-}" == "--dry-run" ]]; then
  dry_run="yes"
  shift
fi

repo="${1:-RayGron/naim-node}"
ruleset_name="${NAIM_MAIN_RULESET_NAME:-main}"
required_check="${NAIM_REQUIRED_PR_CHECK:-NAIM PR Gate / summary}"

ruleset_json="$(gh api "repos/${repo}/rulesets" --jq ".[] | select(.name == \"${ruleset_name}\")")"
if [[ -z "${ruleset_json}" ]]; then
  echo "error: ruleset '${ruleset_name}' not found in ${repo}" >&2
  exit 1
fi

ruleset_id="$(
  RULESET_JSON="${ruleset_json}" python3 - <<'PY'
import json
import os

print(json.loads(os.environ["RULESET_JSON"])["id"])
PY
)"

ruleset_full_json="$(gh api "repos/${repo}/rulesets/${ruleset_id}")"
payload="$(
  RULESET_JSON="${ruleset_full_json}" python3 - "${required_check}" <<'PY'
import json
import os
import sys

required_check = sys.argv[1]
ruleset = json.loads(os.environ["RULESET_JSON"])
rules = ruleset.get("rules") or []
rules = [rule for rule in rules if rule.get("type") != "required_status_checks"]
rules.append({
    "type": "required_status_checks",
    "parameters": {
        "strict_required_status_checks_policy": True,
        "required_status_checks": [
            {
                "context": required_check,
                "integration_id": 15368,
            }
        ],
    },
})
payload = {
    "name": ruleset["name"],
    "target": ruleset["target"],
    "enforcement": ruleset["enforcement"],
    "conditions": ruleset.get("conditions") or {},
    "rules": rules,
    "bypass_actors": ruleset.get("bypass_actors") or [],
}
print(json.dumps(payload))
PY
)"

if [[ "${dry_run}" == "yes" ]]; then
  printf '%s\n' "${payload}"
  exit 0
fi

printf '%s\n' "${payload}" | gh api \
  --method PUT \
  "repos/${repo}/rulesets/${ruleset_id}" \
  --input - >/dev/null

echo "ruleset ${ruleset_name} now requires status check: ${required_check}"
