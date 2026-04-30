#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"
source "${repo_root}/scripts/deploy/naim-production-env.sh"

snapshot_path=""
failed_tag="${NAIM_RELEASE_TAG:-${NAIM_IMAGE_TAG:-}}"
output_path=""

usage() {
  cat <<'EOF'
Usage:
  rollback-main-release.sh --snapshot <path> --failed-tag <tag> [--output <path>]

Rolls production back to the previous release captured before a post-merge
deployment. It first attempts a logical rollback through the controller, then
falls back to the captured compose and SQLite snapshot if needed.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --snapshot)
      snapshot_path="${2:-}"
      shift 2
      ;;
    --failed-tag)
      failed_tag="${2:-}"
      shift 2
      ;;
    --output)
      output_path="${2:-}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown argument '$1'" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ -z "${snapshot_path}" || -z "${failed_tag}" ]]; then
  echo "error: --snapshot and --failed-tag are required" >&2
  usage >&2
  exit 1
fi
if [[ ! -f "${snapshot_path}" ]]; then
  echo "error: snapshot not found: ${snapshot_path}" >&2
  exit 1
fi

previous_tag="$(
  python3 - "${snapshot_path}" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as handle:
    snapshot = json.load(handle)
print(snapshot.get("previous_tag") or "")
PY
)"

if [[ -z "${previous_tag}" ]]; then
  report="rollback skipped: no previous release tag was captured before ${failed_tag}"
  printf '%s\n' "${report}"
  if [[ -n "${output_path}" ]]; then
    printf '%s\n' "${report}" > "${output_path}"
  fi
  exit 2
fi

rollback_status="success"
rollback_detail=""

if ! NAIM_RELEASE_TAG="${previous_tag}" NAIM_IMAGE_TAG="${previous_tag}" \
    bash "${script_dir}/main-bootstrap-controller-update.sh" --tag "${previous_tag}"; then
  rollback_status="fallback-attempted"
  rollback_detail="logical rollback to ${previous_tag} failed; restoring captured compose/database"
  snapshot_b64="$(base64 -w0 "${snapshot_path}")"
  if ! remote_main_bash "${snapshot_b64}" <<'REMOTE'
set -euo pipefail

snapshot_b64="$1"
snapshot_path="$(mktemp)"
printf '%s' "${snapshot_b64}" | base64 -d > "${snapshot_path}"

eval "$(
  python3 - "${snapshot_path}" <<'PY'
import json
import shlex
import sys

with open(sys.argv[1], "r", encoding="utf-8") as handle:
    snapshot = json.load(handle)
for key in [
    "main_root",
    "previous_tag",
    "compose_backup_path",
    "db_backup_path",
    "controller_port",
    "web_ui_port",
]:
    print(f"{key}=" + shlex.quote(str(snapshot.get(key) or "")))
PY
)"

if [[ -z "${main_root}" || -z "${previous_tag}" ]]; then
  echo "rollback fallback snapshot is missing main_root or previous_tag" >&2
  exit 1
fi

release_dir="${main_root}/releases"
compose_path="${main_root}/docker-compose.yml"
db_path="${main_root}/state/controller.sqlite"

if [[ -n "${compose_backup_path}" && -f "${compose_backup_path}" ]]; then
  cp "${compose_backup_path}" "${compose_path}"
else
  echo "captured compose backup is unavailable: ${compose_backup_path}" >&2
  exit 1
fi

if [[ -n "${db_backup_path}" && -f "${db_backup_path}" ]]; then
  docker compose -f "${compose_path}" down >/dev/null 2>&1 || true
  cp "${db_backup_path}" "${db_path}"
  rm -f "${db_path}-wal" "${db_path}-shm"
fi

if [[ -f "${release_dir}/${previous_tag}.json" ]]; then
  ln -sfn "${previous_tag}.json" "${release_dir}/current.json"
  printf '%s\n' "${previous_tag}" > "${release_dir}/current-tag"
  chmod 0640 "${release_dir}/current-tag"
fi

docker compose -f "${compose_path}" up -d --remove-orphans >/dev/null

wait_for_http() {
  local url="$1"
  local attempts="${2:-90}"
  for _ in $(seq 1 "${attempts}"); do
    if curl -fsS "${url}" >/dev/null 2>&1; then
      return 0
    fi
    sleep 1
  done
  echo "timed out waiting for ${url}" >&2
  return 1
}

wait_for_http "http://127.0.0.1:${controller_port}/health" 90
wait_for_http "http://127.0.0.1:${web_ui_port}/health" 90
REMOTE
  then
    rollback_status="failed"
    rollback_detail="logical rollback and captured-state fallback both failed"
  else
    rollback_status="fallback-success"
  fi
fi

if [[ "${rollback_status}" != "failed" ]]; then
  remote_main_bash "${NAIM_MAIN_ROOT}" "${previous_tag}" <<'REMOTE'
set -euo pipefail
main_root="$1"
previous_tag="$2"
release_dir="${main_root}/releases"
if [[ -f "${release_dir}/${previous_tag}.json" ]]; then
  ln -sfn "${previous_tag}.json" "${release_dir}/current.json"
  printf '%s\n' "${previous_tag}" > "${release_dir}/current-tag"
  chmod 0640 "${release_dir}/current-tag"
else
  echo "previous release manifest is missing: ${release_dir}/${previous_tag}.json" >&2
  exit 1
fi
REMOTE
fi

report="$(
  python3 - "${snapshot_path}" "${failed_tag}" "${previous_tag}" "${rollback_status}" "${rollback_detail}" <<'PY'
import json
import sys

snapshot_path, failed_tag, previous_tag, status, detail = sys.argv[1:6]
with open(snapshot_path, "r", encoding="utf-8") as handle:
    snapshot = json.load(handle)

lines = [
    "## Rollback",
    "",
    f"- failed release: `{failed_tag}`",
    f"- restored release: `{previous_tag}`",
    f"- status: `{status}`",
    f"- captured at: `{snapshot.get('captured_at', 'n/a')}`",
    f"- backup root: `{snapshot.get('backup_root', 'n/a')}`",
]
if detail:
    lines.append(f"- detail: {detail}")
print("\n".join(lines))
PY
)"

printf '%s\n' "${report}"
if [[ -n "${output_path}" ]]; then
  printf '%s\n' "${report}" > "${output_path}"
fi

[[ "${rollback_status}" != "failed" ]]
