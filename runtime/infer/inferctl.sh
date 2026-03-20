#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

bin_path="${COMET_INFERCTL_BIN:-}"
if [[ -z "${bin_path}" ]]; then
  if [[ -x /runtime/bin/comet-inferctl ]]; then
    bin_path="/runtime/bin/comet-inferctl"
  else
    read -r host_os host_arch < <("${repo_root}/scripts/detect-host-target.sh")
    candidate="$("${repo_root}/scripts/print-build-dir.sh" "${host_os}" "${host_arch}")/comet-inferctl"
    if [[ -x "${candidate}" ]]; then
      bin_path="${candidate}"
    fi
  fi
fi

if [[ -z "${bin_path}" || ! -x "${bin_path}" ]]; then
  echo "error: comet-inferctl binary not found; build the project or set COMET_INFERCTL_BIN" >&2
  exit 1
fi

export COMET_INFER_PROFILES_PATH="${COMET_INFER_PROFILES_PATH:-${script_dir}/runtime-profiles.json}"
exec "${bin_path}" "$@"
