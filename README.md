# comet-node

Initial implementation scaffold for `comet`, a Docker-oriented replacement for the legacy
Plane / Infer / Worker orchestration flow.

## Current MVP

- `comet-controller` builds a demo desired state and renders per-node `docker-compose.yml`
- `comet-controller` also renders an infer-side runtime manifest that starts to replace legacy `infer.conf`
- `comet-hostd` prints the local disk and container actions it would execute on a node
- `comet-hostd` now writes `infer-runtime.json` into the plane control path on the shared disk for infer nodes
- shared C++ models describe planes, nodes, instances, disks, GPU leases, and compose services
- runtime entrypoint scripts and SQLite migrations are stubbed into the repository layout from day one

This first slice intentionally focuses on:

1. a clean repository structure
2. a buildable C++ control-plane skeleton
3. the path from desired state to `compose` artifacts
4. a SQLite-backed controller state store
5. the first migration step for legacy infer runtime configuration

REST API and Docker execution are the next layers.

## Runtime Images

The repository now includes real runtime Dockerfiles:

- [runtime/base/Dockerfile](/mnt/e/dev/Repos/comet-node/runtime/base/Dockerfile)
- [runtime/infer/Dockerfile](/mnt/e/dev/Repos/comet-node/runtime/infer/Dockerfile)
- [runtime/worker/Dockerfile](/mnt/e/dev/Repos/comet-node/runtime/worker/Dockerfile)

The current base image is `debian:bookworm-slim`. That is intentionally a lightweight
glibc-based base, rather than Alpine, because the next phases need a safer path for
`llama.cpp`-based inference work and native C++ runtime integration.

The demo plane now targets local dev images:

- `comet/base-runtime:dev`
- `comet/infer-runtime:dev`
- `comet/worker-runtime:dev`

Build them with:

```bash
./scripts/build-runtime-images.sh
```

## Dependencies

External C/C++ dependencies are managed through `vcpkg` in manifest mode.

Current manifest dependencies:

- `sqlite3`
- `nlohmann-json`

The host configure script searches for `vcpkg`, installs the manifest dependencies for the
current platform triplet, and only then runs CMake configure.

## Build

```bash
./scripts/configure-host-build.sh Debug
cmake --build "$(./scripts/print-host-build-dir.sh)"
```

The configure step clears the previous CMake cache in the selected build folder so toolchain
and `vcpkg` dependency changes are picked up reliably.

Host builds are grouped by platform and architecture:

- Linux x64: `build/linux/x64`
- Linux arm64: `build/linux/arm64`
- Windows x64: `build/windows/x64`
- macOS arm64: `build/macos/arm64`

The executables and static libraries are emitted directly into that folder.

You can also configure any target layout manually:

```bash
cmake -S . -B build/linux/x64
cmake --build build/linux/x64
```

## VS Code Tasks

The repository ships VS Code build tasks similar to the `maglev` workflow:

- `Comet Linux x64: Build Debug`
- `Comet Linux x64: Build Release`
- `Comet Linux arm64: Build Debug`
- `Comet Linux arm64: Build Release`
- `Comet Windows x64: Build Debug`
- `Comet Windows x64: Build Release`
- `Comet Windows arm64: Build Debug`
- `Comet Windows arm64: Build Release`
- `Comet Linux x64: Package Release`
- `Comet Linux arm64: Package Release`
- `Comet Windows x64: Package Release`
- `Comet Windows arm64: Package Release`
- `Comet: Package Release`
- `Comet: Check`

## Run

Show the demo desired state:

```bash
./build/linux/x64/comet-controller show-demo-plan
```

Initialize the controller SQLite database:

```bash
./build/linux/x64/comet-controller init-db --db var/controller.sqlite
```

Seed the database with the demo desired state:

```bash
./build/linux/x64/comet-controller seed-demo --db var/controller.sqlite
```

Import a plane bundle from files:

```bash
./build/linux/x64/comet-controller import-bundle --bundle config/demo-plane --db var/controller.sqlite
```

Validate a plane bundle without applying it:

```bash
./build/linux/x64/comet-controller validate-bundle --bundle config/demo-plane
```

Show the reconcile plan against the current SQLite state:

```bash
./build/linux/x64/comet-controller plan-bundle --bundle config/demo-plane --db var/controller.sqlite
```

Show the per-node host operations implied by the bundle:

```bash
./build/linux/x64/comet-controller plan-host-ops --bundle config/demo-plane --db var/controller.sqlite --artifacts-root var/artifacts
```

Show the desired state currently stored in SQLite:

```bash
./build/linux/x64/comet-controller show-state --db var/controller.sqlite
```

Render the compose file for all demo nodes:

```bash
./build/linux/x64/comet-controller render-demo-compose
```

Render the compose file from SQLite-backed desired state:

```bash
./build/linux/x64/comet-controller render-compose --db var/controller.sqlite
```

Preview the compose output directly from a bundle before import:

```bash
./build/linux/x64/comet-controller preview-bundle --bundle config/demo-plane --node node-a
```

Apply a bundle into controller state with a reconcile summary:

```bash
./build/linux/x64/comet-controller apply-bundle --bundle config/demo-plane --db var/controller.sqlite --artifacts-root var/artifacts
```

Render the infer runtime manifest from SQLite-backed desired state:

```bash
./build/linux/x64/comet-controller render-infer-runtime --db var/controller.sqlite
```

Inspect the per-node assignments queued for `hostd`:

```bash
./build/linux/x64/comet-controller show-host-assignments --db var/controller.sqlite
```

Inspect the last observed heartbeat and applied state summary reported by each host:

```bash
./build/linux/x64/comet-controller show-host-observations --db var/controller.sqlite
```

Show controller-side host health, including stale-heartbeat detection:

```bash
./build/linux/x64/comet-controller show-host-health --db var/controller.sqlite --stale-after 300
```

Inspect explicit operator overrides for node availability:

```bash
./build/linux/x64/comet-controller show-node-availability --db var/controller.sqlite
```

Mark a node as unavailable so new host assignments stop targeting it:

```bash
./build/linux/x64/comet-controller set-node-availability --db var/controller.sqlite --node node-b --availability unavailable --message "maintenance window"
```

Moving a node out of `active` now also queues a node-local drain assignment so `hostd` can
remove workloads from that node before it is considered fully out of rotation.

When a node returns to `active`, `comet-controller` now auto-enqueues a resync assignment for the
current desired generation if that node missed the latest rollout while it was out of rotation.

Requeue a failed assignment after fixing the underlying issue:

```bash
./build/linux/x64/comet-controller retry-host-assignment --db var/controller.sqlite --id 2
```

Render the compose file for a single node:

```bash
./build/linux/x64/comet-controller render-demo-compose --node node-a
```

Show the local operations `hostd` would perform on one node from demo state:

```bash
./build/linux/x64/comet-hostd show-demo-ops --node node-a
```

Show the local operations `hostd` would perform for one node from controller SQLite state:

```bash
./build/linux/x64/comet-hostd show-state-ops --node node-a --db var/controller.sqlite --artifacts-root var/artifacts --state-root var/hostd-state
```

Inspect the local applied-state snapshot for a node:

```bash
./build/linux/x64/comet-hostd show-local-state --node node-a --state-root var/hostd-state
```

Inspect the infer-side dry-run runtime snapshot materialized under the node's shared disk:

```bash
./build/linux/x64/comet-hostd show-runtime-status --node node-a --state-root var/hostd-state
```

Publish a manual heartbeat from `hostd` back into controller SQLite:

```bash
./build/linux/x64/comet-hostd report-observed-state --node node-a --db var/controller.sqlite --state-root var/hostd-state
```

Apply the local node operations with safe path rebasing for a test runtime root:

```bash
./build/linux/x64/comet-hostd apply-state-ops --node node-a --db var/controller.sqlite --artifacts-root var/artifacts --runtime-root var/runtime --state-root var/hostd-state --compose-mode skip
```

Apply the same node operations and let `hostd` run real `docker compose up -d`:

```bash
./scripts/build-runtime-images.sh
./build/linux/x64/comet-hostd apply-state-ops --node node-a --db var/controller.sqlite --artifacts-root var/artifacts --runtime-root var/runtime --state-root var/hostd-state --compose-mode exec
```

Claim and apply the next queued assignment for one node:

```bash
./build/linux/x64/comet-hostd apply-next-assignment --node node-a --db var/controller.sqlite --runtime-root var/runtime --state-root var/hostd-state --compose-mode skip
```

`apply-bundle` now materializes both per-node `docker-compose.yml` files and a plane-level
`infer-runtime.json` under `var/artifacts/<plane>/`. That JSON intentionally mirrors the
legacy `render-infer-config.sh` output shape closely enough for the next runtime migration
steps around native `llama.cpp` startup.

When `hostd` applies the infer node state, it also writes that runtime manifest into
`<shared-disk>/control/<plane>/infer-runtime.json`, which matches `control_root` inside the
container. The infer entrypoint now advertises `COMET_INFER_RUNTIME_CONFIG` /
`COMET_CONTROL_ROOT` and defaults to `COMET_INFER_BOOT_MODE=launch-runtime`. That path goes
through `comet-inferctl`, which now links `llama.cpp` directly as a library and can bring up
an in-process inference backend as soon as the active model resolves to a local GGUF file.

There is now a first infer-side runtime helper at `runtime/infer/inferctl.sh`:

```bash
./runtime/infer/inferctl.sh list-profiles
./runtime/infer/inferctl.sh validate-config --config var/artifacts/alpha/infer-runtime.json
./runtime/infer/inferctl.sh bootstrap-runtime --config var/artifacts/alpha/infer-runtime.json --profile generic
./runtime/infer/inferctl.sh doctor --config var/artifacts/alpha/infer-runtime.json
./runtime/infer/inferctl.sh preload-model --config var/artifacts/alpha/infer-runtime.json --alias qwen35 --source-model-id Qwen/Qwen3.5-7B-Instruct --local-model-path /tmp/qwen35
./runtime/infer/inferctl.sh cache-status --config var/artifacts/alpha/infer-runtime.json --alias qwen35 --local-model-path /tmp/qwen35
./runtime/infer/inferctl.sh switch-model --config var/artifacts/alpha/infer-runtime.json --model-id Qwen/Qwen3.5-7B-Instruct --runtime-profile qwen3_5 --tp 1 --pp 1
./runtime/infer/inferctl.sh show-active-model --config var/artifacts/alpha/infer-runtime.json
./runtime/infer/inferctl.sh gateway-plan --config var/artifacts/alpha/infer-runtime.json
./runtime/infer/inferctl.sh gateway-status --config var/artifacts/alpha/infer-runtime.json
./runtime/infer/inferctl.sh launch-runtime --config var/artifacts/alpha/infer-runtime.json --backend llama
./runtime/infer/inferctl.sh status --config var/artifacts/alpha/infer-runtime.json
./runtime/infer/inferctl.sh stop --config var/artifacts/alpha/infer-runtime.json
./runtime/infer/inferctl.sh prepare-runtime --config var/artifacts/alpha/infer-runtime.json
./runtime/infer/inferctl.sh plan-launch --config var/artifacts/alpha/infer-runtime.json
```

For host-local `--apply` runs, use a writable copy of `infer-runtime.json` whose `control_root`
and cache/log paths point at a local directory such as `/tmp/comet-infer-model-state/`. When the
host-visible GGUF path differs from the path seen by the runtime container, pass
`--runtime-model-path /comet/shared/.../model.gguf` to `preload-model` so the active model state
stores the runtime-visible mount path instead of the host staging path. `cache-status` and
`show-active-model` become useful after those state files are materialized.

Those commands now cover:
- bundled runtime profile discovery
- config validation
- dry-run bootstrap/profile planning
- local preflight checks
- model-cache registry planning under `control_root`
- active-model manifest planning under `control_root`
- gateway manifest planning under `control_root`
- combined runtime status/readiness reporting
- dry-run stop/reset of active runtime manifests
- directory preparation
- `llama.cpp` / gateway launch planning
- native C++ launch with real `/health`, `/v1/models`, `/v1/completions`, and `/v1/chat/completions` endpoints

`preload-model --apply` and `switch-model --apply` only write local state files under
`control_root` plus a cache marker under the requested model directory. `gateway-plan --apply`
and `status --apply` only write local gateway/runtime manifests. `stop --apply` only clears
those runtime manifests and refreshes the shared runtime snapshot. They still do not download
models by themselves; they prepare the state consumed by the native `llama.cpp` runtime.

When `status --apply` writes `runtime-status.json` under the infer node shared disk, `hostd`
can read the same snapshot through `show-runtime-status`, so the node agent and infer-side
helper now look at one shared runtime state. A follow-up
`comet-hostd report-observed-state` publishes that same runtime snapshot back into controller
SQLite, so `comet-controller show-host-observations` and `show-host-health` can surface
runtime readiness alongside the normal applied-state heartbeat.

`launch-runtime` is now the bridge between dry-run and full runtime. It prefers the in-process
`llama.cpp` backend whenever the active model points at a local GGUF file and keeps
`runtime-status.json` updated with fields such as `runtime_backend`, `runtime_phase`,
`inference_ready`, `gateway_ready`, `started_at`, and `supervisor_pid`.

`hostd` tracks assignment attempts in SQLite. A failed claimed assignment is returned to
`pending` until it exhausts its `max_attempts`, after which it becomes `failed` and can be
manually requeued with `retry-host-assignment`. `hostd` also reports observed status and
heartbeat data back into controller SQLite, so `comet-controller` can show the last known
applied generation and node-local state summary for each host, plus a derived host-health
view that marks stale heartbeats after a configurable threshold. Operator-side node
availability overrides (`active|draining|unavailable`) are stored separately and keep new
assignments away from nodes that were taken out of rotation.

## Repository Layout

- `controller/` - global control-plane executable
- `hostd/` - per-node host agent executable
- `common/` - shared domain models, planner helpers, compose renderer, infer runtime renderer
- `runtime/base/` - shared runtime container base image
- `runtime/infer/` - infer container entrypoint assets
- `runtime/worker/` - worker container entrypoint assets
- `scripts/` - low-level shell helpers
- `config/` - example plane bundles
- `migrations/` - SQLite schema migrations
- `docs/` - architecture notes
