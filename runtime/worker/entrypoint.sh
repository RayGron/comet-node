#!/usr/bin/env bash
set -euo pipefail

mkdir -p /tmp
echo "[comet-worker] booting plane=${COMET_PLANE_NAME:-unknown} instance=${COMET_INSTANCE_NAME:-unknown}"
touch /tmp/comet-ready
exec sleep infinity
