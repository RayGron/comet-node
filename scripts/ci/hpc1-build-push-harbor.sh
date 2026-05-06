#!/usr/bin/env bash
set -euo pipefail

: "${NAIM_RELEASE_SHA:?NAIM_RELEASE_SHA is required}"
: "${NAIM_RELEASE_TAG:?NAIM_RELEASE_TAG is required}"
: "${NAIM_REGISTRY:=chainzano.com}"
: "${NAIM_REGISTRY_PROJECT:=naim}"

current_sha="$(git rev-parse HEAD)"
if [[ "${current_sha}" != "${NAIM_RELEASE_SHA}" ]]; then
  echo "error: hpc1 workspace is at ${current_sha}, expected ${NAIM_RELEASE_SHA}" >&2
  exit 1
fi

docker_config=""
cleanup() {
  if [[ -n "${docker_config}" ]]; then
    rm -rf "${docker_config}"
  fi
  rm -f "${HOME}/.docker/config.json" 2>/dev/null || true
  rmdir "${HOME}/.docker" 2>/dev/null || true
}
trap cleanup EXIT
rm -f "${HOME}/.docker/config.json" 2>/dev/null || true

docker_login_with_retry() {
  local attempt=1
  local max_attempts="${NAIM_REGISTRY_LOGIN_ATTEMPTS:-5}"
  local delay_sec="${NAIM_REGISTRY_LOGIN_RETRY_DELAY_SEC:-5}"

  while (( attempt <= max_attempts )); do
    if docker login "${NAIM_REGISTRY}" \
      -u "${NAIM_REGISTRY_USERNAME}" \
      --password-stdin < "${NAIM_REGISTRY_PASSWORD_FILE}" >/dev/null; then
      return 0
    fi
    if (( attempt == max_attempts )); then
      echo "error: docker login failed after ${max_attempts} attempts" >&2
      return 1
    fi
    echo "warning: docker login failed on attempt ${attempt}/${max_attempts}; retrying in ${delay_sec}s" >&2
    sleep "${delay_sec}"
    delay_sec=$(( delay_sec * 2 ))
    attempt=$(( attempt + 1 ))
  done
}

if [[ -n "${NAIM_REGISTRY_USERNAME:-}" && -n "${NAIM_REGISTRY_PASSWORD_FILE:-}" ]]; then
  docker_config="$(mktemp -d)"
  export DOCKER_CONFIG="${docker_config}"
  docker_login_with_retry
fi

manifest_path="${NAIM_RELEASE_MANIFEST_PATH:-$(pwd)/var/release-manifest-${NAIM_RELEASE_TAG}.json}"
"$(pwd)/scripts/release/build-and-push-images.sh" \
  --registry "${NAIM_REGISTRY}" \
  --project "${NAIM_REGISTRY_PROJECT}" \
  --tag "${NAIM_RELEASE_TAG}" \
  --manifest "${manifest_path}"

echo "harbor manifest=${manifest_path}"
