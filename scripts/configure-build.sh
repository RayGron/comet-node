#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
  echo "usage: configure-build.sh <os> <arch> [build-type]" >&2
  exit 1
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd -- "${script_dir}/.." && pwd)"
target_os="${1}"
target_arch="${2}"
build_type="${3:-Debug}"

case "${build_type}" in
  Debug|Release|RelWithDebInfo|MinSizeRel)
    ;;
  *)
    echo "error: unsupported build type '${build_type}'" >&2
    echo "supported values: Debug, Release, RelWithDebInfo, MinSizeRel" >&2
    exit 1
    ;;
esac

eval "$("${script_dir}/resolve-build-target.sh" "${target_os}" "${target_arch}")"

build_dir="$("${script_dir}/print-build-dir.sh" "${target_os}" "${target_arch}")"
"${script_dir}/ensure-vcpkg-deps.sh" "${VCPKG_TRIPLET}"
vcpkg_installed_dir="${repo_dir}/vcpkg_installed"
ninja_exe="$("${script_dir}/find-ninja.sh")"
cmake_prefix_path="${vcpkg_installed_dir}/${VCPKG_TRIPLET}"

mkdir -p "${build_dir}"
cache_path="${build_dir}/CMakeCache.txt"
needs_clean_reconfigure=0

if [[ -f "${cache_path}" ]]; then
  if ! grep -Fq "CMAKE_BUILD_TYPE:STRING=${build_type}" "${cache_path}"; then
    needs_clean_reconfigure=1
  fi
  if ! grep -Fq "VCPKG_TARGET_TRIPLET:UNINITIALIZED=${VCPKG_TRIPLET}" "${cache_path}" \
    && ! grep -Fq "VCPKG_TARGET_TRIPLET:STRING=${VCPKG_TRIPLET}" "${cache_path}"; then
    needs_clean_reconfigure=1
  fi
  if ! grep -Fq "VCPKG_INSTALLED_DIR:UNINITIALIZED=${vcpkg_installed_dir}" "${cache_path}" \
    && ! grep -Fq "VCPKG_INSTALLED_DIR:PATH=${vcpkg_installed_dir}" "${cache_path}" \
    && ! grep -Fq "VCPKG_INSTALLED_DIR:STRING=${vcpkg_installed_dir}" "${cache_path}"; then
    needs_clean_reconfigure=1
  fi
  if ! grep -Fq "COMET_SKIP_VCPKG_TOOLCHAIN:BOOL=ON" "${cache_path}" \
    && ! grep -Fq "COMET_SKIP_VCPKG_TOOLCHAIN:UNINITIALIZED=ON" "${cache_path}" \
    && ! grep -Fq "COMET_SKIP_VCPKG_TOOLCHAIN:STRING=ON" "${cache_path}"; then
    needs_clean_reconfigure=1
  fi
  if ! grep -Fq "CMAKE_PREFIX_PATH:UNINITIALIZED=${cmake_prefix_path}" "${cache_path}" \
    && ! grep -Fq "CMAKE_PREFIX_PATH:PATH=${cmake_prefix_path}" "${cache_path}" \
    && ! grep -Fq "CMAKE_PREFIX_PATH:STRING=${cmake_prefix_path}" "${cache_path}"; then
    needs_clean_reconfigure=1
  fi
  if ! grep -Fq "CMAKE_GENERATOR:INTERNAL=Ninja" "${cache_path}"; then
    needs_clean_reconfigure=1
  fi
  if ! grep -Fq "CMAKE_MAKE_PROGRAM:FILEPATH=${ninja_exe}" "${cache_path}" \
    && ! grep -Fq "CMAKE_MAKE_PROGRAM:UNINITIALIZED=${ninja_exe}" "${cache_path}" \
    && ! grep -Fq "CMAKE_MAKE_PROGRAM:STRING=${ninja_exe}" "${cache_path}"; then
    needs_clean_reconfigure=1
  fi
fi

if [[ "${needs_clean_reconfigure}" == "1" ]]; then
  echo "[cmake] cache settings changed; recreating ${build_dir}" >&2
  cmake -E rm -f "${cache_path}"
  cmake -E remove_directory "${build_dir}/CMakeFiles"
fi

cmake \
  -S "${repo_dir}" \
  -B "${build_dir}" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE="${build_type}" \
  -DCMAKE_MAKE_PROGRAM="${ninja_exe}" \
  -DCMAKE_PREFIX_PATH="${cmake_prefix_path}" \
  -DCOMET_SKIP_VCPKG_TOOLCHAIN=ON \
  -DVCPKG_TARGET_TRIPLET="${VCPKG_TRIPLET}" \
  -DVCPKG_INSTALLED_DIR="${vcpkg_installed_dir}"

echo "${build_dir}"
