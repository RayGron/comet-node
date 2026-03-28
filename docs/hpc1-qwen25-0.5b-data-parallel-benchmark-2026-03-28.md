# HPC1 Qwen2.5-0.5B Data-Parallel Benchmark

Date: 2026-03-28

## Scope

This benchmark exercised the new `data_parallel_mode=auto_replicas` path on `hpc1` with a small
vLLM plane:

- Model: `Qwen/Qwen2.5-0.5B-Instruct`
- Served model name: `qwen2.5-0.5b-instruct`
- Plane: `qwen25-0.5b-dp-bench`
- Controller: `http://127.0.0.1:18080`
- Benchmark harness: `scripts/benchmark-data-parallel-throughput.py`
- Prompt: `Explain three practical habits for disciplined trading in one concise paragraph.`
- Load shape: `12` concurrent workers x `3` requests each = `36` requests total
- `max_tokens=128`

Each step used the same benchmark harness and the same prompt/load shape. Before each measured run,
the plane was:

1. stopped
2. updated to the new desired state
3. started
4. waited until the interaction endpoint reported the expected replica count
5. warmed with one short request
6. benchmarked

## Topology

### Step 1: Single Worker

- `data_parallel_mode=off`
- `1` worker
- `worker-hostd-a`, GPU `0`

### Step 2: Data-Parallel x2

- `data_parallel_mode=auto_replicas`
- `2` workers
- `worker-hostd-a`, GPU `0`
- `worker-hostd-b`, GPU `2`

### Step 3: Data-Parallel x3

- `data_parallel_mode=auto_replicas`
- `3` workers
- `worker-hostd-a`, GPU `0`
- `worker-hostd-b`, GPU `2`
- `local-hostd`, GPU `3`

### Step 4: Oversubscribe

- `data_parallel_mode=auto_replicas`
- `4` workers on `3` physical GPUs
- `worker-hostd-a`, GPU `0`, exclusive
- `worker-hostd-b`, GPU `2`, exclusive
- `local-hostd`, GPU `3`, shared `0.5`
- `local-hostd`, GPU `3`, shared `0.5`

## Results

Primary metric below is aggregate `completion_tokens/sec`, because it is the clearest proxy for
useful generation throughput. Aggregate `tokens/sec` is included as a secondary metric.

| Step | Topology | Completion tok/s | Total tok/s | Elapsed sec | Requests | Delta vs Step 1 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 1 worker | 615.928 | 1425.244 | 3.914 | 36 | baseline |
| 2 | DP x2 | 78.940 | 181.181 | 30.986 | 36 | -87.2% |
| 3 | DP x3 | 84.288 | 198.890 | 27.643 | 36 | -86.3% |
| 4 | Oversubscribe x4 on 3 GPUs | 83.772 | 198.460 | 27.623 | 36 | -86.4% |

Relative changes between scaled steps:

- Step 3 vs Step 2: `+6.8%` completion tok/s
- Step 4 vs Step 3: `-0.6%` completion tok/s

All four measured runs completed `36/36` requests successfully with `0` benchmark-time failures.

## Readiness Notes

Rollout convergence was materially slower than the steady-state benchmark itself.

- Step 2 converged to `2/2` ready replicas at readiness poll attempt `18`
- Step 3 converged to `3/3` ready replicas at readiness poll attempt `19`
- Step 4 converged to `4/4` ready replicas at readiness poll attempt `19`

With a `5s` poll interval, that places convergence for the scaled steps at roughly `85-95s`.

During Step 3 and Step 4 there was also a short interval where the controller interaction status
lagged behind the latest rollout and briefly reported the previous replica topology before
converging to the new generation.

## Interpretation

On this `hpc1` setup, the small `0.5B` model did **not** benefit from `comet-managed` data
parallel replica groups.

Observed outcome:

- `1` worker was dramatically faster than every scaled configuration
- `2` replicas caused a large drop in throughput
- `3` replicas recovered only a small amount versus `2` replicas
- oversubscribing to `4` workers on `3` GPUs did not improve throughput over `3` replicas

Most likely causes on this host:

- the model is small enough that a single vLLM instance on one GPU already saturates the useful
  throughput for this workload
- each extra replica pays its own startup, scheduling, cache, and runtime overhead
- the `hpc1` environment uses multiple logical hostd nodes on the same physical machine, so this is
  not a clean multi-host scale-out case
- the oversubscribe case adds GPU sharing contention without adding new physical GPU capacity

## Operational Findings

- The new DP replica-group path worked end-to-end on `hpc1` for `2`, `3`, and `4` logical
  replicas.
- The controller and infer runtime eventually converged to the expected replica counts:
  - Step 2: `replica_groups_expected=2`, `replica_groups_ready=2`
  - Step 3: `replica_groups_expected=3`, `replica_groups_ready=3`
  - Step 4: `replica_groups_expected=4`, `replica_groups_ready=4`
- The oversubscribe plan was admitted successfully with shared workers on one GPU.
- The scheduler warned that the oversubscribe layout left idle local GPUs available while a
  soft-share group existed on `local-hostd:3`.

## Conclusion

For this `Qwen2.5-0.5B` workload on `hpc1`, enabling `data_parallel_mode=auto_replicas` reduced
aggregate generation throughput rather than improving it.

The practical conclusion from this run is:

- keep this specific small-model plane on a single worker for best throughput on `hpc1`
- do not use the oversubscribe layout for this workload
- treat larger models or truly multi-host deployments as separate benchmark targets rather than
  extrapolating from this result

## Post-Test State

After the benchmark:

- the temporary plane `qwen25-0.5b-dp-bench` was stopped and scheduled for deletion
- `lt-cypher-ai` remained healthy and reported `ready=true`
- `http://127.0.0.1:18110/cypher/health` remained `status=ok`
