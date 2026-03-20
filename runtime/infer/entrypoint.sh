#!/usr/bin/env bash
set -euo pipefail

mkdir -p /tmp
config_path="${COMET_INFER_RUNTIME_CONFIG:-${COMET_CONTROL_ROOT:-/comet/shared/control/${COMET_PLANE_NAME:-unknown}}/infer-runtime.json}"
boot_mode="${COMET_INFER_BOOT_MODE:-launch-runtime}"
echo "[comet-infer] booting plane=${COMET_PLANE_NAME:-unknown} instance=${COMET_INSTANCE_NAME:-unknown}"
echo "[comet-infer] control_root=${COMET_CONTROL_ROOT:-unknown}"
echo "[comet-infer] runtime_config=${config_path}"
echo "[comet-infer] boot_mode=${boot_mode}"
if [[ -f "${config_path}" ]]; then
  echo "[comet-infer] runtime config found"
  if [[ -x /runtime/bin/comet-inferctl || -x /runtime/infer/inferctl.sh ]]; then
    case "${boot_mode}" in
      prepare-only)
        /runtime/infer/inferctl.sh bootstrap-runtime --config "${config_path}" --apply
        /runtime/infer/inferctl.sh doctor --config "${config_path}"
        /runtime/infer/inferctl.sh plan-launch --config "${config_path}"
        /runtime/infer/inferctl.sh status --config "${config_path}" --apply
        ;;
      validate-only)
        /runtime/infer/inferctl.sh validate-config --config "${config_path}"
        /runtime/infer/inferctl.sh doctor --config "${config_path}"
        /runtime/infer/inferctl.sh plan-launch --config "${config_path}"
        ;;
      launch-embedded)
        /runtime/infer/inferctl.sh bootstrap-runtime --config "${config_path}" --apply
        /runtime/infer/inferctl.sh doctor --config "${config_path}"
        /runtime/infer/inferctl.sh gateway-plan --config "${config_path}" --apply
        /runtime/infer/inferctl.sh plan-launch --config "${config_path}"
        /runtime/infer/inferctl.sh status --config "${config_path}" --apply
        exec /runtime/infer/inferctl.sh launch-embedded-runtime --config "${config_path}"
        ;;
      launch-runtime)
        /runtime/infer/inferctl.sh bootstrap-runtime --config "${config_path}" --apply
        /runtime/infer/inferctl.sh doctor --config "${config_path}"
        /runtime/infer/inferctl.sh gateway-plan --config "${config_path}" --apply
        /runtime/infer/inferctl.sh plan-launch --config "${config_path}"
        /runtime/infer/inferctl.sh status --config "${config_path}" --apply
        exec /runtime/infer/inferctl.sh launch-runtime --config "${config_path}" --backend "${COMET_INFER_RUNTIME_BACKEND:-auto}"
        ;;
      idle)
        echo "[comet-infer] skipping inferctl bootstrap"
        ;;
      *)
        echo "[comet-infer] unsupported boot mode: ${boot_mode}" >&2
        exit 1
        ;;
    esac
  else
    echo "[comet-infer] inferctl helper not found"
  fi
else
  echo "[comet-infer] runtime config not found yet"
fi
touch /tmp/comet-ready
exec sleep infinity
