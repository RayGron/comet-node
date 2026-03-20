# Architecture Notes

## Roles

- `comet-controller` owns desired state, scheduling decisions, reconciliation, and API surface
- `comet-hostd` owns local execution on one node: disks, mounts, compose artifacts, container lifecycle, telemetry

## Current Implementation Boundary

The first implementation slice is intentionally narrow:

- shared domain models exist in `common/`
- a bundle importer reads `plane.json`, `infer.json`, and `workers/*.json`
- the controller can validate and preview bundle results before import
- the controller can compute a reconcile plan against current SQLite state
- the controller can compute explicit per-node host execution plans
- the planner converts desired state into per-node compose plans
- the compose renderer emits deterministic `docker-compose.yml` content
- `apply-bundle` materializes compose artifacts under `var/artifacts/<plane>/<node>/docker-compose.yml`
- `apply-bundle` also materializes `var/artifacts/<plane>/infer-runtime.json` as the first direct replacement for legacy infer-side runtime config rendering
- `hostd` writes that infer runtime manifest into `control_root` on the infer node shared disk, so runtime containers can consume it without depending on the controller artifact path at startup
- `runtime/infer/inferctl.sh` consumes that manifest for validation, bootstrap, model-state planning, and launch control
- `runtime/infer/runtime-profiles.json` and `inferctl.sh bootstrap-runtime|doctor` cover the preflight layer: profile selection, directory preparation, and local readiness checks
- `inferctl.sh preload-model|cache-status|switch-model|show-active-model` now add a dry-run model lifecycle layer on top of that preflight path by materializing cache-registry and active-model state files under `control_root`, still without downloads or process launch
- host-side staging can preserve the runtime-visible GGUF mount path separately from the host-local staging path, so `hostd`-materialized shared disks and the in-container `llama.cpp` loader stay consistent during real deployments
- `inferctl.sh gateway-plan|gateway-status|status|stop` connect that model lifecycle back to gateway wiring and a combined readiness view under `control_root`
- `inferctl.sh launch-runtime` now selects the in-process `llama.cpp` backend whenever the active model resolves to a local GGUF file; that backend is linked directly into `comet-inferctl` as a library, not driven through external `llama-cli` commands
- the local HTTP runtime serves `/health`, `/v1/models`, `/v1/completions`, and `/v1/chat/completions` itself while keeping `runtime-status.json` current
- infer compose healthchecks now probe the live infer HTTP health endpoint instead of only checking for a marker file
- `hostd show-runtime-status` reads the same `runtime-status.json` from the infer node shared disk, so the host agent and infer-side helper share one runtime snapshot instead of maintaining separate ad-hoc summaries
- when `hostd` reports observed state back to the controller, it now includes that serialized runtime snapshot too, so controller-side host observations and health views can show runtime readiness without talking to infer containers directly
- the controller can persist desired state in SQLite
- the controller can queue per-node host assignments in SQLite for `hostd`
- queued host assignments are versioned by desired generation, and older pending/claimed rows are superseded instead of being silently dropped
- queued host assignments track `attempt_count/max_attempts`; transient hostd failures return a claimed assignment to `pending`, while exhausted assignments become `failed` until an operator explicitly requeues them
- controller-side node availability overrides (`active`, `draining`, `unavailable`) are stored separately from desired state and determine whether new host assignments are emitted for a node
- controller-side scheduling policy now validates worker GPU pinning and `gpu_fraction` admission before desired state is persisted, and it produces per-`node/gpu` soft-share summaries for operator review
- when a node leaves `active`, the controller can enqueue a node-specific drain assignment so `hostd` removes node-local workloads
- when a node transitions back to `active`, the controller can enqueue a node-specific resync assignment for the current desired generation if that node missed the latest rollout
- `hostd` reports node-local observed state and heartbeat data back into controller SQLite, including the last applied generation and last assignment id
- `comet-controller` derives a host-health view from those heartbeats so operators can distinguish online, stale, and never-seen nodes
- `hostd` shows the concrete local operations implied by SQLite-backed desired state
- `hostd` can apply node-local filesystem work from desired state, with optional `runtime-root` rebasing for safe local testing
- `hostd` persists node-local applied state so later runs can reconcile deltas instead of always applying from scratch

## Why Start Here

This path exercises the main architecture seam:

`desired state -> node plan -> compose artifact + infer runtime artifact -> host execution`

That seam should stay stable even after the following pieces are added:

- SQLite state and migrations application
- JSON bundle import and validation
- Docker API execution
- REST API, SSE, and UI
- scheduler logic for sharing and draining GPUs
