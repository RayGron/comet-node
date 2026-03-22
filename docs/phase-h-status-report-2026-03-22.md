# Phase H Status Report

Date: 2026-03-22

## Scope Reference

GitHub issue: `#8` `Phase H: Realtime operator web UI and web-ui sidecar`

Phase H goal:

- add a production browser-facing operator surface for `comet`
- deliver that surface through a global `comet-web-ui` sidecar
- move the UI implementation to React
- keep `comet-controller` as the only backend/control-plane process
- support multi-plane browser workflows and plane lifecycle actions

## Executive Summary

Phase H is `in progress`, but its architecture has been replanned.

The earlier Phase H slices remain useful:

- SSE exists
- dashboard/read models exist
- controller static asset serving exists
- a first plain operator UI exists under `ui/operator/`

Those slices are now treated as transitional backend/dev seams, not the final production shape.

The updated production target is:

- browser origin = `comet-web-ui`
- `comet-web-ui` serves the UI and reverse-proxies controller REST/SSE
- React is the UI technology
- `Node.js` is build-time only
- controller/store/planner move to a multi-plane backend model
- `stop-plane` means `scale-to-zero, keep config`

Current implementation status:

- `Stage 0` completed
- `H1` completed:
  - plane registry/read side exists
  - plane-scoped desired-state replace exists
  - plane-scoped desired generation and rebalance iteration exist
  - `list-planes`, `show-plane`, `GET /api/v1/planes`, `GET /api/v1/planes/:plane`, and `GET /api/v1/planes/:plane/dashboard` exist
  - `rollout_actions` are plane-scoped in SQLite and no longer overwrite sibling planes
  - `LoadDesiredState(plane)` now correctly filters dependency/environment/label rows for that plane
- `H2` completed:
  - `start-plane` / `stop-plane` lifecycle actions exist
  - `stop-plane-state` host assignment exists and `hostd` applies it as scale-to-zero while keeping disks
  - `hostd` local applied state is plane-scoped under per-plane state roots, with aggregate compatibility snapshots kept for observation/reporting seams
  - plane-local rollout state no longer clobbers sibling planes and plane-specific controller read views filter `rollout_actions`, `host_assignments`, and `host_observations`
  - controller plane views recover plane membership from aggregate `host_observations` snapshots, so multi-plane nodes remain visible in rollout/rebalance/state inspection
  - node availability drain/resync now fan out per running plane instead of acting on only one implicit desired state
  - smoke now proves that two planes can coexist on the same nodes and can be started/stopped independently without tearing down the sibling plane
- `H3` completed:
  - a new `comet/web-ui:dev` image exists under `runtime/web-ui`
  - controller-managed lifecycle commands exist: `ensure-web-ui`, `show-web-ui-status`, `stop-web-ui`
  - controller materializes sidecar compose/config under `var/web-ui`
  - `build-runtime-images.sh` now builds the web-ui image alongside base/infer/worker images
  - smoke validates compose materialization, sidecar status reporting, and controller event emission for the web-ui lifecycle seam
- `H4` completed:
  - a new React workspace exists under `ui/operator-react`
  - plane list, plane detail, node cards, instance cards, assignment summaries, rollout actions, rebalance plan, disk state, and recent events are rendered from controller APIs
  - the required visual system is now in place: space / cyberspace / comet styling, blue/white/yellow palette, semantic red/yellow/green states, and hollow-circle boot indicators
  - system telemetry cards now cover CPU, GPU, network, and disk summaries per node through plane-scoped host observations
  - the React bundle is validated locally through `npm run build` and through `comet-controller serve --ui-root ui/operator-react/dist`

## Current Valid Foundation

### 1. Controller transport seams already exist

Implemented:

- `comet-controller serve`
- read/write REST API
- SSE stream on top of persisted `event_log`
- compact dashboard payload
- existing static asset serving seam for dev fallback

Assessment:

- `completed as foundation`

### 2. Browser-facing telemetry/event foundations already exist

Implemented:

- runtime status snapshots
- GPU telemetry
- disk telemetry
- network telemetry
- CPU telemetry
- structured event log

Assessment:

- `completed as backend dependency`

### 3. First operator UI exists, but on the wrong production path

Implemented:

- plain static UI under `ui/operator/`
- controller-served SPA fallback
- direct REST + SSE consumption

Problem:

- production architecture no longer targets controller-hosted static UI
- UI technology direction is now React in a separate sidecar container

Assessment:

- `transitional only`

## Replan Outcome

The old Phase H execution order is no longer the right reference.

The updated implementation order is now:

1. documentation/issues realignment
2. multi-plane backend refactor
3. `comet-web-ui` runtime image and lifecycle
4. React UI and browser workflows

## Updated Architecture Contract

### Runtime layers

- `Level 0`: `comet-web-ui`
- `Level 1`: `comet-controller`
- `Level 2`: infer runtime
- `Level 3`: worker runtime

### Browser path

- browser talks only to `comet-web-ui`
- `comet-web-ui` serves UI assets
- `comet-web-ui` reverse-proxies `/api/*`
- `comet-web-ui` reverse-proxies `/api/v1/events/stream`

### UI toolchain

- React is the production UI path
- `Node.js` is allowed only at build time
- there is no Node runtime in the shipped product

### Plane lifecycle semantics

- `stop-plane` = stop workloads and scale runtime to zero
- plane config remains persisted
- `delete-plane` is separate and destructive

## Implementation Plan

### Stage 0. Documentation and issue realignment

Do first:

- update local docs so they no longer describe controller-hosted static UI as the target
- rewrite issue `#8` around:
  - `comet-web-ui`
  - React
  - multi-plane browser workflows
  - `stop-plane` semantics
- update notes on phases `#6`, `#7`, `#9`, `#10` where ownership shifted

Definition of done:

- no roadmap text contradicts the new architecture

### H1. Multi-plane backend refactor

Implement:

- plane registry in SQLite
- per-plane desired state and generation
- per-plane lifecycle state
- plane-scoped assignments, rollout actions, scheduler runtime, disk runtime state, and events
- plane-scoped REST/API surface

Definition of done:

- controller can persist and inspect multiple planes without flattening them into one desired state

### H2. Planner and host execution model for multi-plane

Implement:

- node plans that aggregate multiple planes safely
- plane-scoped runtime roots/artifacts
- plane-aware host assignment semantics
- `stop-plane-state` and `apply-plane-state`

Definition of done:

- two planes can coexist on one node and can be started/stopped independently

### H3. `comet-web-ui` runtime image and lifecycle

Implement:

- new runtime image `comet/web-ui:dev`
- multi-stage build:
  - React build stage
  - minimal runtime/proxy stage
- controller-managed lifecycle:
  - `ensure-web-ui`
  - `show-web-ui-status`
  - `stop-web-ui`

Definition of done:

- controller can materialize and run the UI sidecar as controller-local infrastructure

### H4. React operator UI

Implement:

- new React frontend workspace
- plane list
- plane detail
- node/system summaries
- infer/worker activity
- assignments
- rollout/rebalance
- disk/network/GPU/CPU telemetry
- recent/live events

Styling:

- themes: space / cyberspace / comet
- palette: blue / white / yellow
- semantic colors:
  - green = healthy
  - yellow = warning
  - red = critical
  - hollow circle = booting/starting

Definition of done:

- operator can inspect current cluster state and active plane details from the browser

Current status:

- `completed`
- production UI assets now come from `ui/operator-react`
- browser inspection works for plane list/detail, lifecycle status, node posture, instances, assignments, rollout/rebalance, disk state, recent events, and CPU/GPU/network/disk telemetry
- remaining gaps sit outside H4 and move into later H steps:
  - browser create/apply workflow
  - richer live refresh behavior around mutating actions

### H5. Browser workflow and plane lifecycle actions

Implement:

- create/apply plane from bundle path
- preview plane
- start plane
- stop plane
- inspect rollout/failures

Definition of done:

- operator can manage plane lifecycle without shell access

Current status:

- `completed`
- the React UI now exposes path-based bundle preview/apply alongside `start-plane` and `stop-plane`
- rollout, rebalance, and failure-related surfaces remain plane-scoped in the browser

### H6. CPU telemetry and UI-facing normalization

Implement:

- CPU telemetry in `hostd`
- controller inspection and dashboard normalization
- UI cards/detail panes for CPU alongside GPU/disk/network

Definition of done:

- browser-facing system telemetry includes CPU as a first-class signal

Current status:

- `completed`
- CPU telemetry is now collected in `hostd`, persisted in host observations, normalized in controller payloads, and rendered in the React UI

### H7. Browser-facing event/realtime model

Implement:

- plane-scoped and cluster-scoped SSE filters
- browser-friendly event taxonomy for:
  - plane lifecycle
  - assignments
  - rollout
  - scheduler verify/rollback/manual-intervention
  - host telemetry/health
  - web-ui sidecar lifecycle

Definition of done:

- UI can refresh correctly from SSE-triggered updates without polling every surface continuously

Current status:

- `completed`
- plane-scoped SSE is wired into the React UI as a live refresh trigger
- the dashboard payload carries an operator-facing alert summary for assignment failures, stale nodes, degraded telemetry, rollout follow-up, and runtime-not-ready conditions
- the sidecar proxy path now validates browser-facing SSE with `Last-Event-ID` passthrough

### H8. Validation campaign

Implement:

- smoke for docs/issue sync assumptions
- smoke for multi-plane API/backend
- smoke for sidecar lifecycle
- smoke for React asset serving + proxy
- live validation for:
  - sidecar-proxied REST/SSE
  - plane start/stop
  - realtime updates in browser-facing paths

Definition of done:

- Phase H has backend and live validation similar to phases F and G

Current status:

- `completed`
- `scripts/check-live-phase-h.sh` now validates:
  - `comet/web-ui:dev` image build
  - controller-managed sidecar lifecycle
  - proxied REST through `comet-web-ui`
  - proxied SSE through `comet-web-ui`
  - browser-facing `stop-plane` / `start-plane` transitions

## Current Status Against The New Plan

- `Stage 0`: completed
- `H1`: completed
- `H2`: completed
- `H3`: completed
- `H4`: completed
- `H5`: completed
- `H6`: completed
- `H7`: completed
- `H8`: completed

Current concrete progress beyond the documentation realignment:

- multi-plane plane registry, plane lifecycle state, and plane-scoped desired-state replacement are in place
- hostd now keeps plane-scoped local applied state and supports `stop-plane-state` without tearing down sibling planes
- `comet-web-ui` exists as a controller-managed sidecar with a dedicated runtime image and lifecycle commands
- the React operator UI is now the production browser path and is buildable locally
- browser workflows now include:
  - plane list/detail inspection
  - `start-plane` / `stop-plane`
  - path-based bundle preview/apply
  - rollout/rebalance/disk/event inspection
- plane-scoped SSE is already active in the UI as a refresh trigger
- dashboard alerts now surface the main controller-side failure and readiness signals for operator monitoring
- live validation now exists for the sidecar-proxied browser path through `scripts/check-live-phase-h.sh`
  - those assignments preserve disks but remove runtime services for the plane on the target node
  - smoke now validates that the node-local applied state converges to `instances=0`

Important limitation:

- planner, host assignments, and controller mutations are still fundamentally single-plane
- `start-plane` now re-enqueues plane apply assignments, but the planner/hostd stack still does not aggregate multiple planes onto one node in one composed execution model
- the current stop/start seam is plane-local lifecycle orchestration, not the final multi-plane node aggregation model yet
- this is the first H1 seam, not the full multi-plane refactor yet

## Notes

Current controller-hosted UI features remain useful and should stay working during the transition:

- `GET /api/v1/events/stream`
- `GET /api/v1/dashboard`
- `serve --ui-root`
- `ui/operator/`

But they should now be treated as:

- development fallback
- smoke coverage seam
- backend bring-up aid

not as the final operator product surface.
