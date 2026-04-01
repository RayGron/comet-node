# HPC1 Qwen3.5-9B Data-Parallel Benchmark

Date: 2026-03-28

## Goal

Repeat the same `hpc1` data-parallel benchmark flow for a larger model and check whether
`comet-managed replica groups` improve aggregate throughput for `Qwen/Qwen3.5-9B`.

This pass intentionally mirrored the earlier benchmark structure:

1. single worker baseline
2. `DP x2`
3. `DP x3`
4. oversubscribe `x4` on `3` physical GPUs

## Short Answer

- `9B` behaves very differently from `0.5B`.
- On this workload, data parallelism did improve aggregate throughput.
- The best result was `oversubscribe x4`, which reached `1565.596 completion tok/s`.
- Relative to the single-worker baseline `545.147 completion tok/s`, that is about `2.87x`.
- `DP x2` also improved cleanly.
- `DP x3` improved on throughput, but the scaled-load run was unstable and produced request
  failures.

## Important Operational Note

For this benchmark, scaling a plane while it was already `running` produced misleading partial
states. New replica groups could stay in `replica_group_partial` even though the plane itself
reported `ready=true`.

The reliable lifecycle on `hpc1` was:

1. `apply-state-file`
2. `start-plane`
3. wait for full convergence:
   `reason=ready`, `degraded=false`, `replica_groups_ready == replica_groups_expected`
4. benchmark
5. `stop-plane`
6. repeat with the next topology

That stop/start boundary was necessary to get clean replica rollout on the current controller path.

## Test Setup

Common settings:

- Host: `hpc1`
- Controller: `http://127.0.0.1:18080`
- Model: `Qwen/Qwen3.5-9B`
- Served model name: `qwen3.5-9b`
- Benchmark script: `scripts/benchmark-data-parallel-diagnostic.sh`
- `max_tokens=128`
- unique prompts enabled for every request

Topology by step:

- Step 1: `1` worker on `local-hostd:0`
- Step 2: `2` replica groups on `local-hostd:0` and `worker-hostd-b:2`
- Step 3: `3` replica groups on `local-hostd:0`, `worker-hostd-b:2`, `local-hostd:3`
- Step 4: `4` replica groups on `3` physical GPUs with `worker-c` and `worker-d` sharing
  `local-hostd:3`

The oversubscribe step was intentionally non-optimal. The scheduler itself recommended rebalancing
one shared worker to idle `local-hostd:1`, but that would have violated the explicit oversubscribe
test shape.

## Results

Primary metric below is aggregate `completion_tokens/sec`.

### Baseline

| Step | Topology | Concurrency | Requests | Completion tok/s |
| --- | --- | ---: | ---: | ---: |
| 1 | 1 worker | 12 | 36 | 545.147 |

### Fixed Total Load

Cluster load stayed fixed at `12` concurrent clients.

| Step | Topology | Concurrency | Requests | Completion tok/s | Delta vs baseline |
| --- | --- | ---: | ---: | ---: | ---: |
| 2 | DP x2 | 12 | 36 | 580.068 | +6.4% |
| 3 | DP x3 | 12 | 36 | 593.083 | +8.8% |
| 4 | Oversubscribe x4 on 3 GPUs | 12 | 36 | 749.737 | +37.5% |

### Scaled Total Load

Total concurrency scaled with replica count.

| Step | Topology | Concurrency | Requests | Success | Fail | Completion tok/s | Delta vs baseline |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 2 | DP x2 | 24 | 72 | 72 | 0 | 1108.763 | +103.4% |
| 3 | DP x3 | 36 | 108 | 50 | 58 | 1064.636 | +95.3% |
| 4 | Oversubscribe x4 on 3 GPUs | 48 | 144 | 144 | 0 | 1565.596 | +187.2% |

## Interpretation

This model shows the opposite broad trend from the earlier `0.5B` result.

For `Qwen/Qwen3.5-9B` on `hpc1`:

- fixed-load DP is already slightly beneficial
- scaled-load DP is clearly beneficial
- oversubscribe `x4` produced the strongest aggregate throughput result

So the bigger model does gain from additional replica groups on this deployment.

At the same time, the matrix also shows that stability is not uniform:

- `DP x2` was clean
- `DP x3` reached high throughput, but the scaled-load run dropped `58` requests
- `oversubscribe x4` was both faster and more stable than `DP x3` in this specific run

That means the current takeaway is not simply “more replicas is always better”.
The real result is:

- `9B` can benefit materially from `data_parallel_mode=auto_replicas`
- convergence and runtime stability must still be validated per topology
- `x3` needs separate investigation because it underperformed `x4` on reliability

## Follow-Up: Why X3 Was Unstable

An immediate follow-up investigation was run on the same `x3` topology.

Two focused reproductions were compared:

1. `x3` benchmark right after convergence, with only `1` warmup request
2. the same `x3` benchmark after `3` additional warmup requests

Results:

| Repro | Warmup pattern | Success | Fail | Completion tok/s |
| --- | --- | ---: | ---: | ---: |
| A | `1` warmup request | 108 | 0 | 152.315 |
| B | `1 + 3` warmup requests | 108 | 0 | 1744.332 |

This is the strongest signal from the investigation:

- the instability did **not** reproduce as a steady-state failure
- the same topology became stable once all replicas had clearly seen traffic
- throughput changed by more than `11x` between the cold and warmed runs

The most plausible explanation is now:

- `x3` failures in the original scaled run were caused by per-replica cold-start behavior
- one warmup request was insufficient because the gateway chooses ready replicas with simple
  round-robin selection in [infer_app.cpp](/Users/vladislavhalasiuk/Projects/Repos/comet-node/runtime/infer/src/infer_app.cpp#L181)
- a single warmup request can therefore hit only one replica, leaving the others to absorb their
  first real burst under benchmark load
- once each replica had been exercised, the failures disappeared

So the current best root cause is not “3 replicas are fundamentally unstable”. It is:

- `x3` is sensitive to uneven per-replica warmup
- the benchmark hit cold replicas too early in the original run
- `comet-node` needs replica-aware warmup before taking high-concurrency measurements

This also explains why `x4` could look healthier than `x3` in the benchmark report: the later step
ran after much more model/runtime state had already been warmed on the host.

## Comparison With 0.5B

Compared with
[hpc1-qwen25-0.5b-data-parallel-diagnostic-matrix-2026-03-28.md](/Users/vladislavhalasiuk/Projects/Repos/comet-node/docs/hpc1-qwen25-0.5b-data-parallel-diagnostic-matrix-2026-03-28.md):

- `0.5B` did not benefit from DP on aggregate throughput
- `9B` did benefit

This strongly suggests that model size and per-replica saturation matter a lot for the
`comet-node + vLLM` deployment shape on `hpc1`.

## Key Operational Findings

- Full rollout must be validated by replica counts, not only by the top-level `ready` boolean.
- On this controller path, `stop -> apply -> start` is the trustworthy benchmark lifecycle for
  scale changes.
- A larger model can hide replica-management overhead much better than a tiny one.

## Post-Test State

After the benchmark:

- `qwen35-9b-dp-matrix` was stopped and left in `deleting`
- `lt-cypher-ai` remained healthy with `ready=true`
- production `lt-cypher-ai` still reported `reason=ready`
