#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"
source "${repo_root}/scripts/deploy/naim-production-env.sh"

release_tag="${NAIM_RELEASE_TAG:-${NAIM_IMAGE_TAG:-}}"
output_path=""

usage() {
  cat <<'EOF'
Usage:
  capture-main-release-state.sh --tag <tag> --output <path>

Captures the production state needed to roll back a post-merge release.
The snapshot metadata is written locally; heavy backups remain on main.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tag)
      release_tag="${2:-}"
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

if [[ -z "${release_tag}" || -z "${output_path}" ]]; then
  echo "error: --tag and --output are required" >&2
  usage >&2
  exit 1
fi

mkdir -p "$(dirname -- "${output_path}")"

snapshot_text="$(
  remote_main_bash \
    "${NAIM_MAIN_ROOT}" \
    "${release_tag}" \
    "${NAIM_MAIN_CONTROLLER_LOCAL_PORT}" \
    "${NAIM_MAIN_WEB_UI_LOCAL_PORT}" <<'REMOTE'
set -euo pipefail

main_root="$1"
release_tag="$2"
controller_port="$3"
web_ui_port="$4"
release_dir="${main_root}/releases"
backup_root="${main_root}/release-rollbacks/${release_tag}"
compose_path="${main_root}/docker-compose.yml"
db_path="${main_root}/state/controller.sqlite"

install -d -m 0750 "${backup_root}"

previous_tag=""
if [[ -f "${release_dir}/current-tag" ]]; then
  previous_tag="$(cat "${release_dir}/current-tag")"
fi

previous_manifest_path=""
if [[ -n "${previous_tag}" && -f "${release_dir}/${previous_tag}.json" ]]; then
  previous_manifest_path="${release_dir}/${previous_tag}.json"
  cp "${previous_manifest_path}" "${backup_root}/previous-release-manifest.json"
fi

compose_backup_path=""
if [[ -f "${compose_path}" ]]; then
  compose_backup_path="${backup_root}/docker-compose.before.yml"
  cp "${compose_path}" "${compose_backup_path}"
fi

db_backup_path=""
if [[ -f "${db_path}" ]]; then
  db_backup_path="${backup_root}/controller.sqlite.before"
  if command -v sqlite3 >/dev/null 2>&1; then
    sqlite3 "${db_path}" ".backup '${db_backup_path}'"
  else
    cp "${db_path}" "${db_backup_path}"
    for suffix in -wal -shm; do
      if [[ -f "${db_path}${suffix}" ]]; then
        cp "${db_path}${suffix}" "${db_backup_path}${suffix}"
      fi
    done
  fi
fi

docker_state_path="${backup_root}/docker-ps.before.txt"
if command -v docker >/dev/null 2>&1; then
  docker ps --format '{{.Names}}|{{.Image}}|{{.Status}}' > "${docker_state_path}" || true
fi

python3 - \
  "${main_root}" \
  "${release_tag}" \
  "${backup_root}" \
  "${previous_tag}" \
  "${previous_manifest_path}" \
  "${compose_backup_path}" \
  "${db_backup_path}" \
  "${docker_state_path}" \
  "${controller_port}" \
  "${web_ui_port}" <<'PY'
import json
import sys
from datetime import datetime, timezone

(
    main_root,
    release_tag,
    backup_root,
    previous_tag,
    previous_manifest_path,
    compose_backup_path,
    db_backup_path,
    docker_state_path,
    controller_port,
    web_ui_port,
) = sys.argv[1:11]

print(json.dumps({
    "captured_at": datetime.now(timezone.utc).isoformat(),
    "main_root": main_root,
    "release_tag": release_tag,
    "backup_root": backup_root,
    "previous_tag": previous_tag,
    "previous_manifest_path": previous_manifest_path,
    "compose_backup_path": compose_backup_path,
    "db_backup_path": db_backup_path,
    "docker_state_path": docker_state_path,
    "controller_port": controller_port,
    "web_ui_port": web_ui_port,
}, indent=2, sort_keys=True))
PY
REMOTE
)"

printf '%s\n' "${snapshot_text}" > "${output_path}"
cat "${output_path}"
