# Phase L Status Report (2026-03-22)

## Summary

Phase L is complete.

It reduced the public startup path for `comet-node` to a user-grade flow centered on:

- install
- run
- open Web UI
- load the first plane

It also made the split between `user flow` and `service flow` explicit in the launcher help,
README, and runtime behavior.

## Completed Scope

- `L1` default paths are now the primary launcher contract
- `L2` controller install is self-contained for the normal `controller + local hostd + web-ui` path
- `L3` controller run is self-contained and uses installed config/layout by default
- `L4` Web UI is now the primary first-plane loading path in the user flow
- `L5` `user flow` vs `service flow` is now explicit in help and docs
- `L6` remote `hostd` onboarding remains default-path based and keeps actionable launcher output
- `L7` launcher output now focuses on operator-facing URLs and next steps
- `L8` smoke and live validation now cover the simplified startup UX

## Key Product Changes

- `comet-node install controller --with-hostd --with-web-ui`
  prepares the normal single-machine platform setup without manual directory preparation
- `comet-node run controller`
  can now start from installed config with no explicit DB, state, runtime, artifacts, or UI-root paths
- launcher help now includes quick-start examples for:
  - `controller + local hostd + web-ui`
  - remote `hostd`
- browser-side bundle apply now uses the installed `artifacts_root` during controller startup instead
  of silently falling back to `var/artifacts`
- `comet-node run controller` now supports `--hostd-compose-mode exec|skip`
  so onboarding and Web UI startup can be validated independently from full runtime image builds

## User Flow

Primary local operator flow:

```bash
./build/linux/x64/comet-node install controller --with-hostd --with-web-ui --skip-systemctl
./build/linux/x64/comet-node run controller
```

Then:

- open `http://localhost:18081`
- preview and apply the first plane from the Web UI
- local `hostd` picks up assignments and materializes node-local state automatically

## Service Flow

Service and automation remain supported through:

- launcher `install`, `run`, `service`, and `doctor`
- controller CLI/API
- host registry workflows such as `connect-hostd`, `show-hostd-hosts`, `revoke-hostd`, and `rotate-hostd-key`

These surfaces remain available, but they are no longer the primary onboarding path for human operators.

## Validation

Validated with:

- `./scripts/build-target.sh linux x64 Debug`
- `./scripts/check.sh`
- `./scripts/check-live-phase-l.sh --skip-build --skip-image-build`

Validated behaviors include:

- default-path launcher install/run flow
- installed config-driven controller startup
- browser-facing first-plane load path
- local hostd assignment and observation round-trip after browser-side apply
- explicit user-flow vs service-flow separation in the public launcher/docs surface

## Notes

- `check-live-phase-l.sh` validates Web UI startup with a real `comet-web-ui` sidecar and runs
  local `hostd` in `--hostd-compose-mode skip`. This keeps the live startup UX validation focused
  on onboarding, controller orchestration, and local state materialization without requiring
  infer/worker runtime image builds.
