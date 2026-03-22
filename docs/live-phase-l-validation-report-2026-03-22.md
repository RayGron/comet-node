# Live Phase L Validation Report (2026-03-22)

## Result

`phase-l-live: OK`

## Coverage

- install controller with local `hostd` and `comet-web-ui` using default paths
- run controller from installed config with no explicit DB/state/runtime/artifacts paths
- reach the browser-facing Web UI
- preview and apply the first plane through the Web UI API path
- verify local `hostd` connects automatically and materializes node-local state after plane apply

## Commands

- `./scripts/check-live-phase-l.sh --skip-build --skip-image-build`

## Environment Notes

- this validation uses a real `comet-web-ui` sidecar container
- it runs local `hostd` with `--hostd-compose-mode skip`
- that mode validates startup UX, browser-driven plane load, and local state materialization without
  requiring `comet/infer-runtime:dev` and `comet/worker-runtime:dev` images to be built first
