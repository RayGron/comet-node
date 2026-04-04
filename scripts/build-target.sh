#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
  echo "usage: build-target.sh <os> <arch> [build-type]" >&2
  exit 1
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
target_os="${1}"
target_arch="${2}"
build_type="${3:-Debug}"
cmake_exe="$("${script_dir}/find-cmake.sh")"

"${script_dir}/configure-build.sh" "${target_os}" "${target_arch}" "${build_type}" >/dev/null
"${cmake_exe}" --build "$("${script_dir}/print-build-dir.sh" "${target_os}" "${target_arch}")"
