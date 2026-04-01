# HPC1 Qwen2.5-0.5B Data-Parallel Benchmark Rerun

Date: 2026-03-28

## Purpose

This rerun repeated the original `Qwen/Qwen2.5-0.5B-Instruct` benchmark on `hpc1` to test whether
transient network conditions could have materially distorted the first result.

The same benchmark harness, prompt, concurrency, and four-step topology progression were used:

- Model: `Qwen/Qwen2.5-0.5B-Instruct`
- Served model name: `qwen2.5-0.5b-instruct`
- Plane: `qwen25-0.5b-dp-bench-r2`
- Controller: `http://127.0.0.1:18080`
- Benchmark harness: `scripts/benchmark-data-parallel-throughput.sh`
- Prompt: `Explain three practical habits for disciplined trading in one concise paragraph.`
- Load shape: `12` concurrent workers x `3` requests each = `36` requests total
- `max_tokens=128`

The four measured steps were identical in structure to the first run:

1. `1` worker
2. `2` replica groups
3. `3` replica groups
4. `4` replica groups with oversubscribe on `3` physical GPUs

## Rerun Results

| Step | Topology | Completion tok/s | Total tok/s | Elapsed sec | Requests |
| --- | --- | ---: | ---: | ---: | ---: |
| 1 | 1 worker | 624.479 | 1430.653 | 3.930 | 36 |
| 2 | DP x2 | 76.823 | 179.730 | 30.785 | 36 |
| 3 | DP x3 | 86.375 | 200.677 | 27.716 | 36 |
| 4 | Oversubscribe x4 on 3 GPUs | 85.004 | 198.871 | 27.822 | 36 |

All four measured runs again completed `36/36` requests successfully with `0` benchmark-time
failures.

## Comparison With First Run

Primary comparison uses aggregate `completion_tokens/sec`.

| Step | First run | Rerun | Delta |
| --- | ---: | ---: | ---: |
| 1 | 615.928 | 624.479 | +1.4% |
| 2 | 78.940 | 76.823 | -2.7% |
| 3 | 84.288 | 86.375 | +2.5% |
| 4 | 83.772 | 85.004 | +1.5% |

The result shape was effectively identical across both runs:

- the single-worker baseline was dramatically faster than every DP topology
- `DP x2` remained much slower than baseline
- `DP x3` slightly improved over `DP x2`
- oversubscribe remained roughly flat versus `DP x3`

Observed run-to-run variance stayed within a narrow range of roughly `1-3%`, which is too small to
change the conclusion.

## Readiness and Rollout Notes

The rerun repeated the same operational pattern seen in the first test:

- scaled topologies took much longer to converge than the benchmark itself
- the oversubscribe step spent time in partial readiness before converging to `4/4`
- one shared-GPU worker showed slow initialization and uneven startup, which is consistent with
  oversubscribe overhead rather than external network instability

During oversubscribe rollout, the plane reached:

- `replica_groups_expected=4`
- `replica_groups_ready=4`
- final benchmark completed successfully after convergence

## Interpretation

This rerun does not support the hypothesis that transient internet conditions were the main reason
for the poor DP scaling result.

The more likely explanation remains local runtime behavior on `hpc1`:

- the `0.5B` model is already very efficient on a single GPU
- each extra replica adds its own runtime overhead
- shared-GPU oversubscribe increases initialization and scheduling cost
- this benchmark exercises one physical host with multiple logical nodes, not a clean distributed
  multi-host throughput scale-out

## Conclusion

The rerun confirms the original finding:

- for `Qwen2.5-0.5B-Instruct` on `hpc1`, `data_parallel_mode=auto_replicas` reduces throughput
  instead of increasing it
- the best-performing topology remains the single-worker baseline
- oversubscribe is operationally possible but not beneficial for this workload

The practical takeaway is that the first benchmark result was reproducible, and network jitter was
not the dominant factor.

## Post-Test State

After the rerun:

- the temporary plane `qwen25-0.5b-dp-bench-r2` was stopped and deleted
- `lt-cypher-ai` remained healthy with `ready=true`
- `http://127.0.0.1:18110/cypher/health` remained `status=ok`
