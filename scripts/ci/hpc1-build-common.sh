#!/usr/bin/env bash
set -euo pipefail

naim_ci_require_release_sha() {
  : "${NAIM_RELEASE_SHA:?NAIM_RELEASE_SHA is required}"

  local current_sha
  current_sha="$(git rev-parse HEAD)"
  if [[ "${current_sha}" != "${NAIM_RELEASE_SHA}" ]]; then
    echo "error: hpc1 workspace is at ${current_sha}, expected ${NAIM_RELEASE_SHA}" >&2
    exit 1
  fi

  printf '%s\n' "${current_sha}"
}

naim_ci_prepare_repo() {
  git config --global --add safe.directory "$(pwd)" >/dev/null 2>&1 || true
}

naim_ci_ensure_writable_dir() {
  local path="$1"
  local owner=""
  local has_foreign_entries="0"

  if [[ ! -d "${path}" ]]; then
    return 0
  fi

  owner="$(stat -c '%u' "${path}" 2>/dev/null || true)"
  if find "${path}" \( ! -uid "$(id -u)" -o ! -writable \) -print -quit 2>/dev/null | grep -q .; then
    has_foreign_entries="1"
  fi

  if [[ "${owner}" != "$(id -u)" ]] || [[ ! -w "${path}" ]] || [[ "${has_foreign_entries}" == "1" ]]; then
    if command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
      sudo chown -R "$(id -u):$(id -g)" "${path}"
    else
      echo "error: directory is not writable by $(id -un): ${path}" >&2
      return 1
    fi
  fi

  chmod -R u+rwX "${path}"
}

naim_ci_prepare_shared_vcpkg_cache() {
  local repo_root="$1"
  local vcpkg_root=""
  local relative_path=""
  local candidate_path=""

  vcpkg_root="$("${repo_root}/scripts/find-vcpkg.sh" --root)"
  for relative_path in buildtrees packages installed; do
    candidate_path="${vcpkg_root}/${relative_path}"
    if [[ -e "${candidate_path}" ]]; then
      naim_ci_ensure_writable_dir "$(realpath -e "${candidate_path}")"
    fi
  done
}

naim_ci_prepare_compiler_cache() {
  local repo_root="$1"
  local cache_dir="${NAIM_CCACHE_DIR:-/mnt/shared-storage/naim/cache/ccache/naim-node}"
  local cache_dir_explicit="0"
  local fallback_cache_dir="/tmp/naim-ccache/naim-node"

  if [[ -n "${NAIM_CCACHE_DIR:-}" ]]; then
    cache_dir_explicit="1"
  fi

  if [[ "${NAIM_COMPILER_CACHE:-auto}" =~ ^(OFF|off|0|false|FALSE|no|NO|none|NONE)$ ]]; then
    return 0
  fi

  if ! command -v ccache >/dev/null 2>&1 && ! command -v sccache >/dev/null 2>&1; then
    echo "warning: no compiler cache launcher found on hpc1; install ccache or sccache to enable caching" >&2
    return 0
  fi

  if command -v ccache >/dev/null 2>&1; then
    if ! mkdir -p "${cache_dir}" 2>/dev/null || ! naim_ci_ensure_writable_dir "${cache_dir}"; then
      if [[ "${cache_dir_explicit}" == "1" ]]; then
        echo "error: explicit NAIM_CCACHE_DIR is not writable: ${cache_dir}" >&2
        return 1
      fi
      echo "warning: ccache directory is not writable, falling back to ${fallback_cache_dir}: ${cache_dir}" >&2
      cache_dir="${fallback_cache_dir}"
      mkdir -p "${cache_dir}"
      naim_ci_ensure_writable_dir "${cache_dir}"
    fi
    export CCACHE_DIR="${cache_dir}"
    export CCACHE_BASEDIR="${repo_root}"
    export CCACHE_COMPRESS="${CCACHE_COMPRESS:-true}"
    export CCACHE_SLOPPINESS="${CCACHE_SLOPPINESS:-include_file_ctime,include_file_mtime,time_macros,file_macro}"
    ccache --set-config=max_size="${NAIM_CCACHE_MAX_SIZE:-20G}" >/dev/null 2>&1 || true
  fi
}
