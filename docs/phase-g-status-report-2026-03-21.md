# Phase G Status Report

Date: 2026-03-21

## Scope Reference

GitHub issue: `#7` `Phase G: Extended telemetry and event stream`

Phase G goal:

- expand observability beyond heartbeat status
- collect richer host and runtime telemetry
- store and surface telemetry useful for production scheduling and diagnostics
- introduce a controller-visible event stream for later API / SSE / UI work

## Executive Summary

Phase G is `completed`.

The current codebase already has a meaningful telemetry layer around host observations:

- runtime status snapshots
- per-instance runtime status
- per-GPU telemetry
- scheduler consumption of observed GPU pressure
- controller-side inspection surfaces for those signals

That means the phase is no longer hypothetical. The implementation now covers richer
host/runtime telemetry, normalized disk/network/device diagnostics, and a persisted event stream:

- disk telemetry includes filesystem usage/capacity, mount health, block IO/perf counters, and
  normalized disk fault / availability counters
- network telemetry is implemented as a normalized inventory/link/byte-counter slice
- a structured event stream exists in SQLite and is queryable through CLI and HTTP

## What Is Already Implemented

### 1. Runtime and GPU telemetry are already collected and persisted

Implemented:

- `hostd` collects runtime status snapshots and per-instance runtime state
- `hostd` collects GPU telemetry through NVML when available
- `hostd` falls back to `nvidia-smi` in degraded mode
- host observations persist both runtime and GPU telemetry into SQLite

Relevant files:

- [main.cpp](/mnt/e/dev/Repos/comet-node/hostd/src/main.cpp)
- [runtime_status.h](/mnt/e/dev/Repos/comet-node/common/include/comet/runtime_status.h)
- [runtime_status.cpp](/mnt/e/dev/Repos/comet-node/common/src/runtime_status.cpp)
- [sqlite_store.h](/mnt/e/dev/Repos/comet-node/common/include/comet/sqlite_store.h)
- [sqlite_store.cpp](/mnt/e/dev/Repos/comet-node/common/src/sqlite_store.cpp)

Assessment:

- `completed`

### 2. The controller already surfaces telemetry to operators

Implemented:

- controller parses `runtime_status_json`
- controller parses `gpu_telemetry_json`
- controller inspection surfaces show telemetry in:
  - `show-host-observations`
  - `show-host-health`
  - `show-state`
- the Phase F HTTP layer exposes those same views as JSON

Relevant files:

- [main.cpp](/mnt/e/dev/Repos/comet-node/controller/src/main.cpp)

Assessment:

- `completed`

### 3. Scheduler already consumes actual observed telemetry

Implemented:

- target admission can hold on `compute-pressure`
- target admission can hold on `observed-insufficient-vram`
- telemetry degradation is visible as `telemetry-degraded`
- move verification checks observed runtime readiness and observed GPU ownership

Relevant files:

- [main.cpp](/mnt/e/dev/Repos/comet-node/controller/src/main.cpp)

Assessment:

- `completed`

### 4. Disk telemetry now goes beyond lifecycle reporting

Implemented:

- desired vs realized disk state is persisted
- controller can inspect realized disk lifecycle
- `hostd` samples per-disk filesystem capacity / used / free bytes through `statvfs`
- `hostd` also samples block-device IO counters through `/sys/class/block/<device>/stat` when the
  realized mount source resolves to a block device
- disk telemetry now also tracks normalized fault and availability counters:
  - `fault_count`
  - `warning_count`
  - `perf_counters_available`
  - `io_error_counters_available`
  - `io_error_count` when a device-specific sysfs counter exists
- host observations persist normalized disk telemetry snapshots
- controller inspection surfaces expose disk telemetry through:
  - `show-host-observations`
  - `show-disk-state`
  - `/api/v1/host-observations`
  - `/api/v1/disk-state`
- live validation exists for ensure / teardown / restart reconciliation

Relevant files:

- [sqlite_store.h](/mnt/e/dev/Repos/comet-node/common/include/comet/sqlite_store.h)
- [sqlite_store.cpp](/mnt/e/dev/Repos/comet-node/common/src/sqlite_store.cpp)
- [runtime_status.h](/mnt/e/dev/Repos/comet-node/common/include/comet/runtime_status.h)
- [runtime_status.cpp](/mnt/e/dev/Repos/comet-node/common/src/runtime_status.cpp)
- [main.cpp](/mnt/e/dev/Repos/comet-node/hostd/src/main.cpp)
- [main.cpp](/mnt/e/dev/Repos/comet-node/controller/src/main.cpp)

Assessment:

- `completed`
- this now covers usage/capacity, mount health, and block IO/perf counters

### 5. Validation now covers telemetry and event flow, not only static rendering

Implemented:

- smoke coverage in [check.sh](/mnt/e/dev/Repos/comet-node/scripts/check.sh) for:
  - telemetry serialization and persistence
  - controller CLI / HTTP envelopes for disk and network telemetry
  - persisted event log rendering through CLI and HTTP
- live validation in [check-live-phase-g.sh](/mnt/e/dev/Repos/comet-node/scripts/check-live-phase-g.sh) for:
  - GPU telemetry visibility
  - degraded GPU telemetry fallback via forced NVML disable
  - bundle event visibility
  - host observation telemetry visibility
  - controller-side availability event visibility
  - host assignment failure event visibility

Assessment:

- `completed`

## Final Remaining-Gap Audit

### 1. Network telemetry now exists as a normalized host-observation slice

Implemented:

- per-interface inventory from `/sys/class/net`
- link / oper-state visibility
- RX/TX byte counters
- controller-visible network diagnostics in:
  - `show-host-observations`
  - `/api/v1/host-observations`

Assessment:

- `completed`

### 2. Structured event stream now exists in SQLite

Implemented:

- persisted `event_log`
- hostd-emitted events for:
  - host observations
  - host assignment apply/fail transitions
- controller-emitted events for:
  - bundle import/apply
  - rollout action status / eviction enqueue / retry placement materialization
  - safe-direct rebalance materialization
  - scheduler verification / rollback / manual-intervention transitions
  - node availability changes
  - host assignment retry
- inspection surfaces:
  - `comet-controller show-events`
  - `GET /api/v1/events`

Assessment:

- `completed`

### 3. Disk performance and fault telemetry now exist

Implemented:

- `read_ios`
- `write_ios`
- `read_bytes`
- `write_bytes`
- `io_time_ms`
- `weighted_io_time_ms`
- `io_in_progress`
- `fault_count`
- `warning_count`
- `perf_counters_available`
- `io_error_counters_available`
- `io_error_count` when available from sysfs

Assessment:

- `completed`

## Acceptance Audit

### Acceptance: controller can inspect richer hardware and runtime signals than simple heartbeat status

Status:

- `completed`

Reason:

- runtime status, instance runtime state, GPU device telemetry, and degraded telemetry state are
  already visible in controller views, and disk/network telemetry now also includes IO/perf and
  disk fault/availability counters

### Acceptance: scheduling can consume actual telemetry rather than only static config

Status:

- `completed`

Reason:

- scheduler and move verification already use observed GPU pressure, observed free VRAM, telemetry
  degradation, runtime readiness, and GPU ownership signals

### Acceptance: telemetry and event flow are validated through both smoke and live campaigns

Status:

- `completed`

Reason:

- `check.sh` validates persistence and rendering contracts
- `check-live-phase-g.sh` validates live host observation telemetry, degraded GPU fallback, and
  event emission through both CLI and HTTP

### Acceptance: telemetry is shaped for API and SSE use later

Status:

- `completed`

Reason:

- runtime status, per-instance runtime status, GPU telemetry, degraded telemetry state, and disk
  telemetry now have stable controller-facing envelopes across CLI and HTTP inspection surfaces

## Practical Status

Current Phase G status:

- GPU/runtime telemetry: strong
- scheduler telemetry integration: strong
- disk telemetry: strong
- network telemetry: strong
- event stream: strong

Suggested GitHub issue status:

- keep `#7` as `in progress`

## Recommended Phase G Plan

### G1. Stabilize telemetry envelopes and controller-facing contracts

Implement:

- define explicit controller-visible payloads for:
  - runtime status
  - per-instance runtime status
  - GPU telemetry
  - degraded telemetry state
- ensure telemetry fields are stable across CLI and HTTP inspection surfaces

Definition of done:

- telemetry output is no longer just "whatever hostd stored", but a stable controller contract

### G2. Extend disk telemetry beyond lifecycle state

Implement:

- filesystem capacity / used / free bytes for mounted managed disks
- mount health / verification status
- disk usage reporting in host observations and controller views

Definition of done:

- controller can inspect not only realized mounts, but also actual disk usage state

### G3. Add network telemetry collection

Implement:

- interface inventory
- link state
- RX/TX byte counters
- optional listen-port / service reachability summary where useful

Definition of done:

- host observations include a first useful network telemetry slice

### G4. Introduce a structured event log

Implement:

- persisted controller events for:
  - desired generation changes
  - assignment lifecycle
  - rollout lifecycle
  - scheduler lifecycle
  - availability changes
- persisted host events for:
  - apply start/finish/failure
  - disk lifecycle transitions
  - telemetry degradation / recovery

Definition of done:

- important control-plane and host transitions are queryable as typed events

### G5. Surface telemetry and events through inspection APIs

Implement:

- CLI and HTTP views for:
  - extended telemetry
  - recent events
- keep contracts aligned with later SSE / UI work

Definition of done:

- Phase G data is ready to be reused by later REST / SSE / UI phases

### G6. Add validation campaign for telemetry and events

Implement:

- smoke tests for telemetry serialization / storage / controller rendering
- live tests for:
  - GPU telemetry collection
  - degraded telemetry fallback
  - disk telemetry
  - event emission on controller / hostd transitions

Definition of done:

- telemetry and event stream behavior are validated end-to-end

## Conclusion

Phase G is not empty; substantial GPU/runtime telemetry work is already present in the current
codebase and is already used by the scheduler. The phase is still not complete, though, because
the broader observability goal requires disk usage telemetry, network telemetry, and a structured
event stream.
