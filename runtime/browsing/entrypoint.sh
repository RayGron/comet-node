#!/usr/bin/env bash
set -euo pipefail

status_path="${COMET_BROWSING_RUNTIME_STATUS_PATH:-/comet/private/browsing-runtime-status.json}"
state_root="${COMET_BROWSING_STATE_ROOT:-/comet/private/sessions}"

mkdir -p "$(dirname "${status_path}")"
mkdir -p "${state_root}"

exec /runtime/bin/comet-browsingd
