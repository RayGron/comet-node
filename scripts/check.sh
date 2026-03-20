#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
read -r host_os host_arch < <("${script_dir}/detect-host-target.sh")
build_dir="$("${script_dir}/print-build-dir.sh" "${host_os}" "${host_arch}")"
db_path="${PWD}/var/controller.sqlite"
artifacts_root="${PWD}/var/artifacts"
runtime_root="${PWD}/var/runtime"
state_root="${PWD}/var/hostd-state"
bad_state_root="${PWD}/var/hostd-state-blocker"
infer_model_root="${PWD}/var/infer-model-state"
infer_model_config="${infer_model_root}/infer-runtime.json"
runtime_infer_config="${runtime_root}/runtime-infer-local.json"
parallel_db_path="${PWD}/var/controller-parallel.sqlite"
parallel_artifacts_root="${PWD}/var/artifacts-parallel"
parallel_runtime_root="${PWD}/var/runtime-parallel"
parallel_state_root="${PWD}/var/hostd-state-parallel"

cmake -E rm -f "${db_path}"
cmake -E rm -f "${parallel_db_path}"
cmake -E remove_directory "${artifacts_root}"
cmake -E remove_directory "${parallel_artifacts_root}"
cmake -E remove_directory "${runtime_root}"
cmake -E remove_directory "${parallel_runtime_root}"
cmake -E remove_directory "${state_root}"
cmake -E remove_directory "${parallel_state_root}"
cmake -E remove_directory "${infer_model_root}"
cmake -E rm -f "${bad_state_root}"

"${script_dir}/build-target.sh" "${host_os}" "${host_arch}" Debug

"${build_dir}/comet-controller" show-demo-plan >/dev/null
"${build_dir}/comet-controller" render-demo-compose --node node-a >/dev/null
"${build_dir}/comet-controller" validate-bundle --bundle "${PWD}/config/demo-plane" >/dev/null
"${build_dir}/comet-controller" preview-bundle --bundle "${PWD}/config/demo-plane" --node node-a >/dev/null
"${build_dir}/comet-controller" init-db --db "${db_path}" >/dev/null
"${build_dir}/comet-controller" plan-bundle --bundle "${PWD}/config/demo-plane" --db "${db_path}" >/dev/null
"${build_dir}/comet-controller" plan-host-ops --bundle "${PWD}/config/demo-plane" --db "${db_path}" --artifacts-root "${artifacts_root}" >/dev/null
"${build_dir}/comet-controller" apply-bundle --bundle "${PWD}/config/demo-plane" --db "${db_path}" --artifacts-root "${artifacts_root}" >/dev/null
"${build_dir}/comet-controller" show-node-availability --db "${db_path}" | grep -F "(empty)" >/dev/null
"${build_dir}/comet-controller" show-host-assignments --db "${db_path}" >/dev/null
"${build_dir}/comet-controller" show-host-observations --db "${db_path}" >/dev/null
"${build_dir}/comet-controller" show-host-health --db "${db_path}" | grep -F "health=unknown" >/dev/null
"${build_dir}/comet-controller" show-state --db "${db_path}" >/dev/null
"${build_dir}/comet-controller" render-infer-runtime --db "${db_path}" | grep -F '"gpu_nodes"' >/dev/null
"${build_dir}/comet-controller" render-compose --db "${db_path}" --node node-a >/dev/null
test -f "${artifacts_root}/alpha/node-a/docker-compose.yml"
test -f "${artifacts_root}/alpha/infer-runtime.json"
/mnt/e/dev/Repos/comet-node/runtime/infer/inferctl.sh validate-config --config "${artifacts_root}/alpha/infer-runtime.json" | grep -F "infer runtime config: OK" >/dev/null
/mnt/e/dev/Repos/comet-node/runtime/infer/inferctl.sh list-profiles | grep -F "generic" >/dev/null
test ! -e /mnt/e/dev/Repos/comet-node/runtime/infer/http_probe.py
test ! -e /mnt/e/dev/Repos/comet-node/runtime/infer/runtime_supervisor.py
test ! -e /mnt/e/dev/Repos/comet-node/runtime/infer/runtime_launcher.py
/mnt/e/dev/Repos/comet-node/runtime/infer/inferctl.sh bootstrap-runtime --config "${artifacts_root}/alpha/infer-runtime.json" --profile generic | grep -F "runtime_mode=llama-library" >/dev/null
/mnt/e/dev/Repos/comet-node/runtime/infer/inferctl.sh plan-launch --config "${artifacts_root}/alpha/infer-runtime.json" | grep -F "primary-infer-local-worker=node:node-a worker:worker-a" >/dev/null
mkdir -p "${infer_model_root}"
perl -MJSON::PP -e '
  use strict;
  use warnings;
  use utf8;
  use Cwd qw(abs_path);
  use File::Spec;

  my ($src, $dst, $root) = @ARGV;
  open my $in, "<:raw", $src or die "open $src: $!";
  local $/;
  my $json_text = <$in>;
  close $in;

  my $data = JSON::PP->new->utf8->decode($json_text);
  my $control_root = File::Spec->catdir($root, "control");
  $data->{plane}->{control_root} = $control_root;
  $data->{control}->{root} = $control_root;

  my %path_map = (
    models_root => File::Spec->catdir($root, "models"),
    gguf_cache_dir => File::Spec->catdir($root, "models", "gguf"),
    infer_log_dir => File::Spec->catdir($root, "logs", "infer"),
  );

  for my $key (keys %path_map) {
    $data->{inference}->{$key} = $path_map{$key};
  }

  open my $out, ">:raw", $dst or die "open $dst: $!";
  print {$out} JSON::PP->new->utf8->canonical->pretty->encode($data);
  close $out;
' "${artifacts_root}/alpha/infer-runtime.json" "${infer_model_config}" "${infer_model_root}"
/mnt/e/dev/Repos/comet-node/runtime/infer/inferctl.sh preload-model --config "${infer_model_config}" --alias qwen35 --source-model-id Qwen/Qwen3.5-7B-Instruct --local-model-path "${infer_model_root}/models/qwen35" --apply | grep -F "preload-model-plan:" >/dev/null
/mnt/e/dev/Repos/comet-node/runtime/infer/inferctl.sh cache-status --config "${infer_model_config}" --alias qwen35 --local-model-path "${infer_model_root}/models/qwen35" | grep -F "registry=present" >/dev/null
/mnt/e/dev/Repos/comet-node/runtime/infer/inferctl.sh switch-model --config "${infer_model_config}" --model-id Qwen/Qwen3.5-7B-Instruct --tp 1 --pp 1 --gpu-memory-utilization 0.85 --runtime-profile qwen3_5 --apply | grep -F "runtime_profile=qwen3_5" >/dev/null
/mnt/e/dev/Repos/comet-node/runtime/infer/inferctl.sh gateway-plan --config "${infer_model_config}" --apply | grep -F "upstream_models_url=http://127.0.0.1:8000/v1/models" >/dev/null
/mnt/e/dev/Repos/comet-node/runtime/infer/inferctl.sh gateway-status --config "${infer_model_config}" | grep -F "active_model=Qwen/Qwen3.5-7B-Instruct served=Qwen3.5-7B-Instruct" >/dev/null
/mnt/e/dev/Repos/comet-node/runtime/infer/inferctl.sh status --config "${infer_model_config}" | grep -F "runtime_phase=planned" >/dev/null
/mnt/e/dev/Repos/comet-node/runtime/infer/inferctl.sh status --config "${infer_model_config}" | grep -F "launch_ready=yes" >/dev/null
/mnt/e/dev/Repos/comet-node/runtime/infer/inferctl.sh show-active-model --config "${infer_model_config}" | grep -F '"model_id": "Qwen/Qwen3.5-7B-Instruct"' >/dev/null
"${build_dir}/comet-hostd" show-demo-ops --node node-b >/dev/null
"${build_dir}/comet-hostd" report-observed-state --db "${db_path}" --node node-b --state-root "${state_root}" >/dev/null
"${build_dir}/comet-controller" show-host-observations --db "${db_path}" --node node-b | grep -F "status=idle" >/dev/null
"${build_dir}/comet-controller" show-host-health --db "${db_path}" --node node-b | grep -F "health=online status=idle" >/dev/null
"${build_dir}/comet-hostd" show-state-ops --db "${db_path}" --node node-a --artifacts-root "${artifacts_root}" --runtime-root "${runtime_root}" --state-root "${state_root}" | grep -F "write-infer-runtime" >/dev/null
"${build_dir}/comet-hostd" apply-state-ops --db "${db_path}" --node node-a --artifacts-root "${artifacts_root}" --runtime-root "${runtime_root}" --state-root "${state_root}" --compose-mode skip >/dev/null
"${build_dir}/comet-hostd" show-runtime-status --node node-a --state-root "${state_root}" | grep -F "runtime_status: empty" >/dev/null
perl -MJSON::PP -e '
  use strict;
  use warnings;
  use utf8;
  use File::Spec;

  my ($src, $dst, $runtime_root) = @ARGV;
  open my $in, "<:raw", $src or die "open $src: $!";
  local $/;
  my $json_text = <$in>;
  close $in;

  my $data = JSON::PP->new->utf8->decode($json_text);
  my $shared_root = File::Spec->catdir($runtime_root, "var", "lib", "comet", "disks", "planes", "alpha", "shared");
  my $control_root = File::Spec->catdir($shared_root, "control", "alpha");
  $data->{plane}->{control_root} = $control_root;
  $data->{control}->{root} = $control_root;

  my %path_map = (
    models_root => File::Spec->catdir($shared_root, "models"),
    gguf_cache_dir => File::Spec->catdir($shared_root, "models", "gguf"),
    infer_log_dir => File::Spec->catdir($shared_root, "logs", "infer"),
  );

  for my $key (keys %path_map) {
    $data->{inference}->{$key} = $path_map{$key};
  }

  open my $out, ">:raw", $dst or die "open $dst: $!";
  print {$out} JSON::PP->new->utf8->canonical->pretty->encode($data);
  close $out;
' "${artifacts_root}/alpha/infer-runtime.json" "${runtime_infer_config}" "${runtime_root}"
/mnt/e/dev/Repos/comet-node/runtime/infer/inferctl.sh preload-model --config "${runtime_infer_config}" --alias qwen35 --source-model-id Qwen/Qwen3.5-7B-Instruct --local-model-path "${runtime_root}/var/lib/comet/disks/planes/alpha/shared/models/qwen35" --apply | grep -F "preload-model-plan:" >/dev/null
/mnt/e/dev/Repos/comet-node/runtime/infer/inferctl.sh switch-model --config "${runtime_infer_config}" --model-id Qwen/Qwen3.5-7B-Instruct --tp 1 --pp 1 --gpu-memory-utilization 0.85 --runtime-profile qwen3_5 --apply | grep -F "runtime_profile=qwen3_5" >/dev/null
/mnt/e/dev/Repos/comet-node/runtime/infer/inferctl.sh gateway-plan --config "${runtime_infer_config}" --apply | grep -F "upstream_models_url=http://127.0.0.1:8000/v1/models" >/dev/null
/mnt/e/dev/Repos/comet-node/runtime/infer/inferctl.sh status --config "${runtime_infer_config}" --apply | grep -F "launch_ready=yes" >/dev/null
"${build_dir}/comet-hostd" show-runtime-status --node node-a --state-root "${state_root}" | grep -F "launch_ready=yes" >/dev/null
"${build_dir}/comet-hostd" report-observed-state --db "${db_path}" --node node-a --state-root "${state_root}" >/dev/null
"${build_dir}/comet-controller" show-host-observations --db "${db_path}" --node node-a | grep -F "runtime_launch_ready=yes runtime_model=Qwen/Qwen3.5-7B-Instruct" >/dev/null
/mnt/e/dev/Repos/comet-node/runtime/infer/inferctl.sh stop --config "${runtime_infer_config}" --apply | grep -F "launch_ready=no" >/dev/null
"${build_dir}/comet-hostd" show-runtime-status --node node-a --state-root "${state_root}" | grep -F "launch_ready=no" >/dev/null
"${build_dir}/comet-hostd" report-observed-state --db "${db_path}" --node node-a --state-root "${state_root}" >/dev/null
"${build_dir}/comet-controller" show-host-observations --db "${db_path}" --node node-a | grep -F "runtime_launch_ready=no runtime_model=(empty)" >/dev/null
"${build_dir}/comet-hostd" show-state-ops --db "${db_path}" --node node-b --artifacts-root "${artifacts_root}" --runtime-root "${runtime_root}" --state-root "${state_root}" >/dev/null
"${build_dir}/comet-controller" show-host-assignments --db "${db_path}" --node node-b | grep -F "attempts=0/3 type=apply-node-state status=pending" >/dev/null
: > "${bad_state_root}"
if "${build_dir}/comet-hostd" apply-next-assignment --db "${db_path}" --node node-b --runtime-root "${runtime_root}" --state-root "${bad_state_root}" --compose-mode skip >/dev/null 2>&1; then
  echo "check: expected first blocked assignment attempt to fail" >&2
  exit 1
fi
"${build_dir}/comet-controller" show-host-assignments --db "${db_path}" --node node-b | grep -F "attempts=1/3 type=apply-node-state status=pending" >/dev/null
"${build_dir}/comet-controller" show-host-observations --db "${db_path}" --node node-b | grep -F "status=failed" >/dev/null
if "${build_dir}/comet-hostd" apply-next-assignment --db "${db_path}" --node node-b --runtime-root "${runtime_root}" --state-root "${bad_state_root}" --compose-mode skip >/dev/null 2>&1; then
  echo "check: expected second blocked assignment attempt to fail" >&2
  exit 1
fi
"${build_dir}/comet-controller" show-host-assignments --db "${db_path}" --node node-b | grep -F "attempts=2/3 type=apply-node-state status=pending" >/dev/null
if "${build_dir}/comet-hostd" apply-next-assignment --db "${db_path}" --node node-b --runtime-root "${runtime_root}" --state-root "${bad_state_root}" --compose-mode skip >/dev/null 2>&1; then
  echo "check: expected third blocked assignment attempt to fail" >&2
  exit 1
fi
"${build_dir}/comet-controller" show-host-assignments --db "${db_path}" --node node-b | grep -F "attempts=3/3 type=apply-node-state status=failed" >/dev/null
"${build_dir}/comet-controller" retry-host-assignment --db "${db_path}" --id 2 >/dev/null
"${build_dir}/comet-controller" show-host-assignments --db "${db_path}" --node node-b | grep -F "attempts=3/4 type=apply-node-state status=pending" >/dev/null
"${build_dir}/comet-hostd" apply-next-assignment --db "${db_path}" --node node-b --runtime-root "${runtime_root}" --state-root "${state_root}" --compose-mode skip >/dev/null
"${build_dir}/comet-hostd" show-local-state --node node-b --state-root "${state_root}" >/dev/null
"${build_dir}/comet-hostd" show-state-ops --db "${db_path}" --node node-b --artifacts-root "${artifacts_root}" --runtime-root "${runtime_root}" --state-root "${state_root}" >/dev/null
"${build_dir}/comet-controller" show-host-assignments --db "${db_path}" --node node-b | grep -F "attempts=4/4 type=apply-node-state status=applied" >/dev/null
"${build_dir}/comet-controller" show-host-observations --db "${db_path}" --node node-b | grep -F "status=applied applied_generation=1 last_assignment_id=2" >/dev/null
"${build_dir}/comet-controller" show-host-health --db "${db_path}" --node node-b | grep -F "health=online status=applied applied_generation=1" >/dev/null
"${build_dir}/comet-controller" set-node-availability --db "${db_path}" --node node-b --availability unavailable --message "check maintenance" >/dev/null
"${build_dir}/comet-controller" show-node-availability --db "${db_path}" --node node-b | grep -F "availability=unavailable" >/dev/null
"${build_dir}/comet-controller" show-host-assignments --db "${db_path}" --node node-b | grep -F "type=drain-node-state status=pending" >/dev/null
"${build_dir}/comet-controller" plan-bundle --bundle "${PWD}/config/demo-plane" --db "${db_path}" | grep -F "skipped_nodes=node-b(unavailable)" >/dev/null
"${build_dir}/comet-controller" set-node-availability --db "${db_path}" --node node-b --availability active --message "back online" >/dev/null
"${build_dir}/comet-controller" show-host-assignments --db "${db_path}" --node node-b | grep -F "generation=2 attempts=0/3 type=apply-node-state status=pending" >/dev/null

"${build_dir}/comet-controller" init-db --db "${parallel_db_path}" >/dev/null
"${build_dir}/comet-controller" apply-bundle --bundle "${PWD}/config/demo-plane" --db "${parallel_db_path}" --artifacts-root "${parallel_artifacts_root}" >/dev/null
(
  "${build_dir}/comet-hostd" apply-next-assignment --db "${parallel_db_path}" --node node-a --runtime-root "${parallel_runtime_root}" --state-root "${parallel_state_root}" --compose-mode skip >/dev/null
) &
parallel_pid_a=$!
(
  "${build_dir}/comet-hostd" apply-next-assignment --db "${parallel_db_path}" --node node-b --runtime-root "${parallel_runtime_root}" --state-root "${parallel_state_root}" --compose-mode skip >/dev/null
) &
parallel_pid_b=$!
wait "${parallel_pid_a}"
wait "${parallel_pid_b}"
"${build_dir}/comet-controller" show-host-assignments --db "${parallel_db_path}" --node node-a | grep -F "status=applied" >/dev/null
"${build_dir}/comet-controller" show-host-assignments --db "${parallel_db_path}" --node node-b | grep -F "status=applied" >/dev/null

test -d "${runtime_root}/var/lib/comet/disks/planes/alpha/shared"
test -f "${runtime_root}/var/lib/comet/disks/planes/alpha/shared/control/alpha/infer-runtime.json"
test -f "${infer_model_root}/control/model-cache-registry.json"
test -f "${infer_model_root}/control/active-model.json"
test -f "${infer_model_root}/control/gateway-plan.json"
test -f "${runtime_root}/var/lib/comet/disks/planes/alpha/shared/control/alpha/runtime-status.json"
test -d "${runtime_root}/var/lib/comet/disks/instances/worker-b/private"
test -f "${artifacts_root}/alpha/node-b/docker-compose.yml"
test -f "${state_root}/node-b/applied-state.json"
test -f "${state_root}/node-b/applied-generation.txt"

echo "check: OK"
