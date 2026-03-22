# Live Phase K Validation Report (2026-03-22)

## Result

`phase-k-live: OK`

## Coverage

- simulated LAN controller/hostd deployment on one machine using separate config, state, and identity roots
- encrypted host-agent transport
- host registration visibility
- assignment apply round-trip
- observation upload round-trip
- host key rotation with old-key rejection
- host revoke with session rejection
- `controller + local hostd` mode through the same host-agent path
- `systemd-analyze verify` for generated unit files

## Commands

- `./scripts/check-live-phase-k.sh --skip-build`

## Environment Notes

- validation is designed for a one-GPU machine
- networked deployment is simulated as local-LAN traffic over localhost endpoints
- service-manager validation uses rendered units plus `systemd-analyze verify`; the current environment does not run an active `systemd` manager
