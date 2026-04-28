#!/usr/bin/env bash
set -euo pipefail

: "${NAIM_RELEASE_SHA:?NAIM_RELEASE_SHA is required}"
: "${NAIM_HPC1_REPO_PATH:?NAIM_HPC1_REPO_PATH is required}"
: "${NAIM_HPC1_SSH:?NAIM_HPC1_SSH is required}"
: "${NAIM_HPC1_SSH_PORT:?NAIM_HPC1_SSH_PORT is required}"
: "${NAIM_CI_SSH_OPTS:?NAIM_CI_SSH_OPTS is required}"
: "${GITHUB_OUTPUT:?GITHUB_OUTPUT is required}"

event_name="${GITHUB_EVENT_NAME:-}"
before_sha="${NAIM_RELEASE_BEFORE:-}"
zero_sha='0000000000000000000000000000000000000000'
turboquant_artifact_check="${NAIM_TURBOQUANT_ARTIFACT_CHECK:-native}"

native_build_required="false"
native_build_reason="lightweight"
native_matched_file=""
turboquant_required="false"
turboquant_reason="cache-reuse"
turboquant_matched_file=""
changed_files=""

is_native_build_sensitive_path() {
  local path="$1"
  case "${path}" in
    CMakeLists.txt|vcpkg.json|vcpkg-configuration.json)
      return 0
      ;;
    cmake/*|cmake/**)
      return 0
      ;;
    *.c|*.cc|*.cpp|*.cxx|*.cu|*.cuh|*.h|*.hh|*.hpp|*.hxx)
      return 0
      ;;
    common/*|common/**|controller/*|controller/**|hostd/*|hostd/**|launcher/*|launcher/**)
      return 0
      ;;
    runtime/*|runtime/**)
      return 0
      ;;
    Dockerfile|*/Dockerfile|**/Dockerfile|docker/*|docker/**)
      return 0
      ;;
  esac

  return 1
}

is_turboquant_sensitive_path() {
  local path="$1"
  case "${path}" in
    CMakeLists.txt|vcpkg.json|vcpkg-configuration.json)
      return 0
      ;;
    cmake/*|cmake/**)
      return 0
      ;;
    runtime/infer/*|runtime/infer/**|runtime/worker/*|runtime/worker/**)
      return 0
      ;;
  esac

  return 1
}

if [[ "${event_name}" == "workflow_dispatch" ]]; then
  native_build_required="true"
  native_build_reason="workflow_dispatch"
  turboquant_required="true"
  turboquant_reason="workflow_dispatch"
else
  if [[ -z "${before_sha}" || "${before_sha}" == "${zero_sha}" ]]; then
    native_build_required="true"
    native_build_reason="missing_base_sha"
    turboquant_required="true"
    turboquant_reason="missing_base_sha"
  else
    changed_files="$(git diff --name-only "${before_sha}" "${NAIM_RELEASE_SHA}")"
    while IFS= read -r path; do
      [[ -n "${path}" ]] || continue
      if [[ "${native_build_required}" != "true" ]] && is_native_build_sensitive_path "${path}"; then
        native_build_required="true"
        native_build_reason="path_change"
        native_matched_file="${path}"
      fi
      if [[ "${turboquant_required}" != "true" ]] && is_turboquant_sensitive_path "${path}"; then
        turboquant_required="true"
        turboquant_reason="path_change"
        turboquant_matched_file="${path}"
      fi
    done <<< "${changed_files}"
  fi
fi

if [[ "${turboquant_required}" != "true" ]] \
  && [[ "${native_build_required}" == "true" || "${turboquant_artifact_check}" == "always" ]]; then
  turboquant_bin_dir="${NAIM_HPC1_REPO_PATH}/build-turboquant/linux/x64/bin"
  if ! ssh ${NAIM_CI_SSH_OPTS} -p "${NAIM_HPC1_SSH_PORT}" "${NAIM_HPC1_SSH}" \
      "test -x $(printf '%q' "${turboquant_bin_dir}/llama-server") && test -x $(printf '%q' "${turboquant_bin_dir}/rpc-server")"; then
    turboquant_required="true"
    turboquant_reason="artifacts_missing"
  fi
fi

{
  printf 'native_build_required=%s\n' "${native_build_required}"
  printf 'native_build_reason=%s\n' "${native_build_reason}"
  printf 'native_matched_file=%s\n' "${native_matched_file}"
  printf 'turboquant_required=%s\n' "${turboquant_required}"
  printf 'turboquant_reason=%s\n' "${turboquant_reason}"
  printf 'turboquant_matched_file=%s\n' "${turboquant_matched_file}"
} >> "${GITHUB_OUTPUT}"

echo "Native hpc1 build required: ${native_build_required}"
echo "Native reason: ${native_build_reason}"
if [[ -n "${native_matched_file}" ]]; then
  echo "Native matched path: ${native_matched_file}"
fi
echo "TurboQuant build required: ${turboquant_required}"
echo "TurboQuant reason: ${turboquant_reason}"
if [[ -n "${turboquant_matched_file}" ]]; then
  echo "TurboQuant matched path: ${turboquant_matched_file}"
fi
if [[ -n "${changed_files}" ]]; then
  echo "Changed files:"
  printf '%s\n' "${changed_files}"
fi

if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
  {
    echo "## Release Scope"
    echo
    echo "- Native hpc1 build required: \`${native_build_required}\`"
    echo "- Native reason: \`${native_build_reason}\`"
    if [[ -n "${native_matched_file}" ]]; then
      echo "- Native matched path: \`${native_matched_file}\`"
    fi
    echo "- TurboQuant build required: \`${turboquant_required}\`"
    echo "- TurboQuant reason: \`${turboquant_reason}\`"
    if [[ -n "${turboquant_matched_file}" ]]; then
      echo "- TurboQuant matched path: \`${turboquant_matched_file}\`"
    fi
  } >> "${GITHUB_STEP_SUMMARY}"
fi
