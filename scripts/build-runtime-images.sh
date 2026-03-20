#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"

base_tag="${1:-comet/base-runtime:dev}"
infer_tag="${2:-comet/infer-runtime:dev}"
worker_tag="${3:-comet/worker-runtime:dev}"

echo "building ${base_tag}"
docker build \
  -f "${repo_root}/runtime/base/Dockerfile" \
  -t "${base_tag}" \
  "${repo_root}"

echo "building ${infer_tag}"
docker build \
  -f "${repo_root}/runtime/infer/Dockerfile" \
  -t "${infer_tag}" \
  "${repo_root}"

echo "building ${worker_tag}"
docker build \
  -f "${repo_root}/runtime/worker/Dockerfile" \
  -t "${worker_tag}" \
  "${repo_root}"

echo "runtime images ready"
echo "  base=${base_tag}"
echo "  infer=${infer_tag}"
echo "  worker=${worker_tag}"

