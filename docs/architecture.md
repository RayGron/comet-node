# Architecture Notes

`naim-node` now sits inside a two-part product architecture:

- `comet` is the control point and operator-facing management surface
- `naim-node` is the managed host agent package that realizes work on connected nodes

This repository contains the current implementation of both sides:

- `comet-controller` is the current implementation of the `comet` control point
- `comet-hostd` is the current implementation of the `naim-node` host agent
- `naim-node` is the launcher that can install and run either role, including a co-located setup

The default remote-host pattern is no longer "controller reaches directly into every host". The
canonical deployment model is that `naim-node` dials out to `comet`, which allows managed nodes to
live behind NAT, firewalls, or dynamic addressing without requiring a permanent public IP.

## Product Split

`comet` owns:

- node onboarding and registry
- desired state, plane lifecycle, scheduling, and reconciliation
- model-library workflows, including download, conversion, and quantization orchestration
- authenticated HTTP APIs, operator UI, and browser-facing read models
- cluster telemetry aggregation, node role derivation, and plane participation views

`naim-node` owns:

- local inventory scans for CPU, RAM, disk, GPU, and runtime posture
- local realization of compose artifacts, runtime configs, disks, and containers
- node-local telemetry and observed-state reporting
- the secure outbound connection back to `comet`

## Node Onboarding And Roles

Managed node lifecycle:

1. the operator adds a node in `comet`
2. `comet` generates a random onboarding key for that node
3. the operator starts `naim-node` with that key in its local configuration
4. `naim-node` authenticates and opens the outbound channel to `comet`
5. if TLS/SSL already protects that channel, no extra stream-encryption layer is required
6. otherwise the host-agent channel must apply its own stream encryption
7. the node is scanned on connect and rescanned every hour
8. `comet` derives the node role from the latest observed inventory and may change that role after
   later rescans

Canonical role rules:

- `Storage`
  - no GPU
  - RAM `< 32 GB`
  - disk capacity `> 100 GB`
- `Worker`
  - one or more GPUs
  - RAM `>= 64 GB`
  - disk capacity `> 100 GB`

Nodes that do not match either rule remain connected and observable, but they are not eligible for
role-dependent placement until later scans show a matching inventory.

## Placement Contracts

### Model Library

Model import and quantization are `comet` workflows, but they are realized on connected
`naim-node` hosts.

Current architectural contract:

- when a model is uploaded or imported, the operator chooses a destination node
- only nodes with enough free capacity for the full resulting artifact are eligible targets
- without quantization, eligible targets may be either `Storage` or `Worker`
- with quantization, eligible targets must be `Worker`
- when quantization is requested, the same `Worker` both stores and quantizes the model
- the node list shown in `comet` must expose role, capacity, telemetry, and current plane
  participation so that placement is explainable

### Plane Placement

Current plane deployment contract:

- each plane currently chooses exactly one primary `naim-node`
- by default all plane containers run on that selected node
- `app` containers are the exception and may be deployed to an external host over SSH
- when a plane runs replicated `skills-<plane>` containers, they must live on the same machine as
  the plane's `app` container rather than on the primary `naim-node`
- plane creation must capture the SSH address plus either a key path or username/password for that
  external app host
- plane creation still captures worker count and soft GPU allocation intent
- workers may share GPUs; the allocation is soft rather than exclusive

The current implementation keeps worker count and infer topology tightly coupled, but the hard
validator guarantee today is narrower: for replica-parallel `llama.cpp + llama_rpc`, `runtime.workers`
must be divisible by `infer.replicas`. Documentation should not overstate this as a universal
`worker_count == infer_count` invariant.

## Runtime And Data Flow

The main architecture seam is:

`desired state -> node registry + scheduler -> host assignments -> naim-node realization -> telemetry/observations -> controller-derived readiness`

The interaction seam remains controller-owned:

`client request -> comet controller session lookup/restore -> prompt reconstruction -> runtime inference -> controller persistence + response shaping`

Runtime containers are still materialized from controller-rendered artifacts and node-local runtime
configs, but the networked node registry is now a first-class architectural layer between
controller policy and runtime execution.

## Supported Topologies

The architecture must support all of these as normal cases:

- remote `comet` with many outbound-connected `naim-node` agents
- mixed clusters where some nodes are `Storage` and others are `Worker`
- a co-located install where `comet` and `naim-node` run on the same machine

In the co-located case, the machine participates in the node registry like any other managed node.
Co-location is a supported deployment shape, not a special debug-only shortcut.

## Related Long-Form Docs

The long-form canonical architecture set lives in [`../naim-docs/`](../naim-docs):

- [`overview/naim-node-overview.md`](../naim-docs/overview/naim-node-overview.md)
- [`design/naim-node-design.md`](../naim-docs/design/naim-node-design.md)
- [`architecture/detailed-architecture.md`](../naim-docs/architecture/detailed-architecture.md)
- [`architecture/component-reference.md`](../naim-docs/architecture/component-reference.md)
