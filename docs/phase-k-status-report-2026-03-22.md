# Phase K Status Report (2026-03-22)

## Summary

Phase K is complete.

It moved `comet-node` from a development-style multi-binary startup model to a production-oriented launcher and service model with:

- one public launcher binary: `comet-node`
- controller-only and `controller + local hostd` modes
- remote `hostd` over controller-owned HTTP host-agent APIs
- host registry, signed session open, encrypted host-agent envelopes, replay guards, session TTL, rekey threshold, revoke, and host-key rotation
- Linux/WSL2-oriented service install and unit verification

## Completed Scope

- `K1` single public launcher binary
- `K2` stable installed config/state layout
- `K3` Linux/WSL2 service install and lifecycle surface
- `K4` remote host-agent API replacing runtime shared-DB coupling
- `K5` controller-side host registry and inspection
- `K6` inner security:
  - long-lived host identity keys
  - signed session open
  - encrypted request/response envelopes
  - replay guard via sequence numbers
  - session expiry and message-count rekey threshold
  - revoke and host-key rotation
- `K7` production image-driven install path as the intended launcher model
- `K8` controller-only and `controller + local hostd` modes
- `K9` smoke and live validation

## Validation

Validated with:

- `./scripts/build-target.sh linux x64 Debug`
- `./scripts/check.sh`
- `./scripts/check-live-phase-k.sh --skip-build`

Validated behaviors include:

- launcher install/service surface
- `systemd-analyze verify` for rendered units
- simulated LAN `controller(A) ↔ hostd(B)` on one machine with separate roots and identities
- `controller + local hostd` on one machine
- signed and encrypted host-agent session flow
- host key rotation
- host revoke

## Notes

- In the current repository workspace under `/mnt/e/...`, `systemd-analyze verify` reports permission warnings for unit files on the mounted filesystem. Verification still passes. This is a filesystem characteristic of the test path, not a launcher or unit-format failure.
