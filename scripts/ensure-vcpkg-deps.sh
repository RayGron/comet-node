#!/usr/bin/env bash
set -euo pipefail

triplet="${1:-}"
if [[ -z "${triplet}" ]]; then
  echo "usage: ensure-vcpkg-deps.sh <triplet>" >&2
  exit 1
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd -- "${script_dir}/.." && pwd)"
vcpkg_root="$("${script_dir}/find-vcpkg.sh" --root)"
vcpkg_exe="${vcpkg_root}/vcpkg"
install_root="${repo_dir}/vcpkg_installed"
stamp_dir="${install_root}/vcpkg"
stamp_path="${stamp_dir}/.comet-${triplet}.manifest.sha256"

if [[ ! -x "${vcpkg_exe}" ]]; then
  echo "error: vcpkg executable is missing at '${vcpkg_exe}'" >&2
  exit 1
fi

if [[ ! -f "${vcpkg_root}/scripts/buildsystems/vcpkg.cmake" ]]; then
  echo "error: vcpkg toolchain file was not found under '${vcpkg_root}'" >&2
  exit 1
fi

hash_cmd=()
if command -v sha256sum >/dev/null 2>&1; then
  hash_cmd=(sha256sum)
elif command -v shasum >/dev/null 2>&1; then
  hash_cmd=(shasum -a 256)
else
  echo "error: unable to find sha256sum or shasum for vcpkg manifest fingerprinting" >&2
  exit 1
fi

manifest_input="$(
  {
    printf 'triplet=%s\n' "${triplet}"
    printf 'vcpkg_root=%s\n' "${vcpkg_root}"
    printf 'vcpkg_exe=%s\n' "${vcpkg_exe}"
    cat "${repo_dir}/vcpkg.json"
    if [[ -f "${repo_dir}/vcpkg-configuration.json" ]]; then
      printf '\n-- vcpkg-configuration.json --\n'
      cat "${repo_dir}/vcpkg-configuration.json"
    fi
  } | "${hash_cmd[@]}" | awk '{print $1}'
)"

if [[ -f "${stamp_path}" && -d "${install_root}/${triplet}" ]]; then
  recorded_manifest="$(<"${stamp_path}")"
  if [[ "${recorded_manifest}" == "${manifest_input}" ]]; then
    echo "[vcpkg] root=${vcpkg_root}"
    echo "[vcpkg] triplet=${triplet}"
    echo "[vcpkg] install_root=${install_root}"
    echo "[vcpkg] manifest unchanged; skipping install"
    exit 0
  fi
fi

echo "[vcpkg] root=${vcpkg_root}"
echo "[vcpkg] triplet=${triplet}"
echo "[vcpkg] install_root=${install_root}"
"${vcpkg_exe}" install \
  --x-manifest-root="${repo_dir}" \
  --x-install-root="${install_root}" \
  --x-wait-for-lock \
  --triplet="${triplet}"

mkdir -p "${stamp_dir}"
printf '%s\n' "${manifest_input}" > "${stamp_path}"
