#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"

bin_path="${COMET_DEVTOOL_BIN:-}"
if [[ -z "${bin_path}" ]]; then
  read -r host_os host_arch < <("${repo_root}/scripts/detect-host-target.sh")
  candidate="$("${repo_root}/scripts/print-build-dir.sh" "${host_os}" "${host_arch}")/comet-devtool"
  if [[ -x "${candidate}" ]]; then
    bin_path="${candidate}"
  fi
fi

if [[ -z "${bin_path}" || ! -x "${bin_path}" ]]; then
  echo "error: comet-devtool binary not found; build the project or set COMET_DEVTOOL_BIN" >&2
  exit 1
fi

exec "${bin_path}" "$@"
