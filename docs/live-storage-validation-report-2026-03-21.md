# Live Storage Validation Report

Date: 2026-03-21

## Summary

Phase E live storage validation completed successfully on the current Linux/WSL host by running
the new live harness:

```bash
./scripts/check-live-storage.sh --skip-build
```

The campaign used:

- real mounted disk lifecycle through `hostd`
- root execution via `wsl.exe -u root`
- compact test bundle sizes (`1 GiB` shared/private images) to fit the local test environment

## What Was Verified

### 1. Real mounted volumes

Verified successfully:

- plane shared disk mounted as a real filesystem
- infer private disk mounted as a real filesystem
- worker private disk mounted as a real filesystem

Controller verification path:

- `comet-controller show-disk-state`

Runtime verification path:

- `mountpoint -q` on mounted host paths

### 2. Container visibility of mounted volumes

Verified successfully:

- a Docker container saw the mounted shared disk at the expected bind-mounted path
- a Docker container saw the mounted infer private disk at the expected bind-mounted path
- files written through the container were visible on the mounted host volume afterward

### 3. Restart reconciliation

Verified successfully:

- `disk_runtime_state` rows were deleted deliberately
- `hostd apply-state-ops` was re-run on the same desired generation
- `hostd` reconstructed realized disk lifecycle state from actual host disk reality
- controller-side `show-disk-state` again reported `realized_state=mounted`

### 4. Worker private teardown and cleanup

Verified successfully:

- a reduced bundle with `worker-b` removed was applied
- `hostd` reconciled `node-b`
- `worker-b-private` was unmounted and removed
- controller reported `realized_state=removed`
- the old worker private mount no longer existed as a mountpoint

### 5. Infer private teardown

Verified successfully:

- infer was moved from `node-a` to `node-b`
- old `infer-main-private` on `node-a` was removed
- new `infer-main-private` on `node-b` was mounted
- controller reported removed/orphan runtime state for the old infer-private path and mounted
  runtime state for the new one

### 6. Plane shared teardown

Verified successfully:

- a fresh `alpha` plane was materialized in a separate runtime root
- a `beta` plane replacement was applied afterward
- old `plane-alpha-shared` was torn down
- new `plane-beta-shared` was mounted
- controller reported `plane-alpha-shared` as removed/orphan runtime state and
  `plane-beta-shared` as mounted

## Important Fixes Found During Live Validation

The live campaign surfaced and validated fixes for these storage issues:

1. Shared-disk image identity on a simulated multi-node single host

- `plane-shared` had to reuse one managed image path across nodes
- otherwise `node-b` saw drift because the same mountpoint was paired with a different image

2. Real disk teardown fallback robustness

- `RemoveDisk` now prefers real teardown whenever the target path is actually mounted
- it no longer depends entirely on a perfectly present runtime-state row

3. Runtime state materialization during removal

- reduced-bundle teardown exposed a bookkeeping gap that caused SQLite foreign-key failure
- the remove path now fills in missing plane/node identity before persisting removed runtime state

4. Disk identity changes under the same `disk_name@node`

- plane replacement exposed that `execution_plan` previously treated some private disk changes as
  `ensure` only
- changed-disk planning now materializes `remove + ensure`

5. Orphan runtime-state persistence across plane replacement

- `disk_runtime_state` could not safely retain removed/orphan rows while still referencing
  `planes`
- the runtime-state table now keeps `plane_name` as data but no longer enforces a foreign key to
  the current declared plane set

## Outcome

This live run confirms that the current Phase E slice now supports:

- real mounted-volume creation
- controller-visible realized disk state
- restart reconciliation
- worker private teardown
- infer private teardown
- plane shared teardown

on the tested Linux/WSL environment.

## Environment Note

The live harness includes a one-shot retry for root `hostd apply-state-ops` because WSL-backed
loop mounts can occasionally report a transient first-apply failure after topology change even
though the follow-up apply converges correctly. The validated end state is still real and
controller-visible.
