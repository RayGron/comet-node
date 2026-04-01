#!/usr/bin/env bash
set -euo pipefail

mkdir -p /tmp
boot_mode="${COMET_WORKER_BOOT_MODE:-llama-idle}"
echo "[comet-worker] booting plane=${COMET_PLANE_NAME:-unknown} instance=${COMET_INSTANCE_NAME:-unknown}"
echo "[comet-worker] control_root=${COMET_CONTROL_ROOT:-unknown}"
echo "[comet-worker] boot_mode=${boot_mode}"

case "${boot_mode}" in
  llama-load|llama-idle|llama-rpc)
    exec /runtime/bin/comet-workerd
    ;;
  *)
    echo "[comet-worker] unsupported boot mode: ${boot_mode}" >&2
    exit 1
    ;;
esac
