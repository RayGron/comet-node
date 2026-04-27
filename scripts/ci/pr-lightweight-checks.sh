#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${repo_root}"

zero_sha='0000000000000000000000000000000000000000'
if [[ -n "${NAIM_BASE_SHA:-}" && -n "${NAIM_RELEASE_SHA:-}" && "${NAIM_BASE_SHA}" != "${zero_sha}" ]]; then
  git diff --check "${NAIM_BASE_SHA}" "${NAIM_RELEASE_SHA}"
else
  git diff --check
fi

while IFS= read -r -d '' script_path; do
  bash -n "${script_path}"
done < <(find scripts -type f -name '*.sh' -print0)

python3 - <<'PY'
from pathlib import Path
import sys

try:
    import yaml
except Exception as exc:
    print(f"PyYAML unavailable; skipped workflow YAML parse: {exc}")
    raise SystemExit(0)

paths = sorted(Path(".github").glob("**/*.yml")) + sorted(Path(".github").glob("**/*.yaml"))
for path in paths:
    yaml.safe_load(path.read_text(encoding="utf-8"))
print(f"parsed {len(paths)} GitHub YAML files")
PY
