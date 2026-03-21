# Phase E Status Report

Date: 2026-03-21

## Scope Reference

GitHub issue: `#5` `Phase E: Virtual disk lifecycle and storage productionization`

Phase E goal:

- replace the old directory-backed execution seam with a real Linux virtual-disk lifecycle
- preserve the current disk model and shared/private semantics
- keep SQLite authoritative for both declared and realized storage state

## Executive Summary

Phase E is now functionally complete.

The codebase now contains:

- declared disk state in SQLite
- separate persisted runtime disk state in SQLite
- real Linux disk lifecycle in `hostd`
  - sparse image creation
  - `losetup` attach/detach
  - `mkfs.ext4`
  - mount/unmount
- safe teardown for removed disks
- restart reconciliation that rebuilds runtime disk state from host reality
- controller-visible `show-disk-state` reporting
- live validation for real mounted volumes, restart reconciliation, infer-private teardown, and
  plane-shared teardown

The current implementation still keeps an explicit `directory-backed-fallback` for unprivileged
dev/smoke flows, but the privileged path is now the real production storage path.

## Step Audit

### E1. Runtime disk state in SQLite

Status:

- `completed`

What is implemented:

- `disk_runtime_state` persists realized lifecycle separately from declared `virtual_disks`
- runtime state stores image path, filesystem type, loop device, mount point, runtime state,
  verification timestamps, and status message

Relevant files:

- [sqlite_store.h](/mnt/e/dev/Repos/comet-node/common/include/comet/sqlite_store.h)
- [sqlite_store.cpp](/mnt/e/dev/Repos/comet-node/common/src/sqlite_store.cpp)
- [0001_initial.sql](/mnt/e/dev/Repos/comet-node/migrations/0001_initial.sql)

### E2. Real ensure path

Status:

- `completed`

What is implemented:

- `hostd` can create sparse image files
- attach them to loop devices
- format them as `ext4`
- mount them at the desired host paths

Relevant files:

- [main.cpp](/mnt/e/dev/Repos/comet-node/hostd/src/main.cpp)

### E3. Safe teardown

Status:

- `completed`

What is implemented:

- `hostd` tears disks down with:
  - `umount`
  - `losetup -d`
  - mount path cleanup
  - managed image cleanup
- removal ordering is safe with `compose down` before disk teardown

Relevant files:

- [main.cpp](/mnt/e/dev/Repos/comet-node/hostd/src/main.cpp)

### E4. Restart reconciliation

Status:

- `completed`

What is implemented:

- `hostd` can rebuild runtime disk state from real image/loop/mount state after restart
- missing `disk_runtime_state` rows can be reconstructed without changing desired generation

Relevant files:

- [main.cpp](/mnt/e/dev/Repos/comet-node/hostd/src/main.cpp)
- [check-live-storage.sh](/mnt/e/dev/Repos/comet-node/scripts/check-live-storage.sh)

### E5. Controller-visible runtime disk reporting

Status:

- `completed`

What is implemented:

- `comet-controller show-disk-state`
- `show-state` includes both desired disk inventory and realized disk runtime state
- removed/orphan runtime rows remain visible after teardown

Relevant files:

- [main.cpp](/mnt/e/dev/Repos/comet-node/controller/src/main.cpp)

### E6. Live storage validation

Status:

- `completed`

What was validated live:

- plane shared disk mounted as a real filesystem
- infer private disk mounted as a real filesystem
- worker private disk mounted as a real filesystem
- bind-mounted containers can see and write into mounted volumes
- restart reconciliation reconstructs runtime disk state
- worker private teardown removes the old mount
- infer private teardown is validated through live infer relocation
- plane shared teardown is validated through live plane rename

Relevant files:

- [check-live-storage.sh](/mnt/e/dev/Repos/comet-node/scripts/check-live-storage.sh)
- [live-storage-validation-report-2026-03-21.md](/mnt/e/dev/Repos/comet-node/docs/live-storage-validation-report-2026-03-21.md)

## Important Implementation Notes

- `disk_runtime_state` no longer has a foreign key to `planes`, because removed/orphan runtime rows
  must survive plane replacement and teardown reporting.
- `execution_plan` now treats disk identity changes under the same `disk_name@node` as
  `remove + ensure`, not just `ensure`.
- the live harness includes a controlled retry for root `hostd apply-state-ops` because WSL loop
  mount reporting can occasionally be transient on the first apply after topology change.

## Conclusion

Phase E is ready to be marked `completed`.
