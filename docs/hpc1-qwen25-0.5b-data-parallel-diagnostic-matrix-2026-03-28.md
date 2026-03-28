# HPC1 Qwen2.5-0.5B Data-Parallel Diagnostic Matrix

Date: 2026-03-28

## Goal

The original and rerun benchmarks showed that `data_parallel_mode=auto_replicas` reduced
throughput for `Qwen/Qwen2.5-0.5B-Instruct` on `hpc1`.

This diagnostic pass was designed to answer four specific questions:

1. Was the original result distorted by external network conditions?
2. Was the single-worker result inflated mainly by repeated identical prompts and prefix caching?
3. Did the first benchmark underfeed each replica by keeping total cluster concurrency fixed?
4. Is the `comet-node` replica balancer actually distributing requests across workers?

## Short Answer

- External internet was not the cause.
- Prefix caching mattered, but it was not the dominant cause.
- Fixed cluster load was a major cause.
- The load balancer did distribute requests across replicas.
- Even after correcting for prompt uniqueness and scaling total load with replica count, aggregate
  DP throughput still remained far below the single-worker baseline.

That means the remaining problem is now narrowed to the runtime architecture and per-replica
serving efficiency, not a broken benchmark and not a broken balancer.

## Test Setup

Common settings:

- Host: `hpc1`
- Controller: `http://127.0.0.1:18080`
- Model: `Qwen/Qwen2.5-0.5B-Instruct`
- Served model name: `qwen2.5-0.5b-instruct`
- Benchmark script: [benchmark-data-parallel-diagnostic.py](/Users/vladislavhalasiuk/Projects/Repos/comet-node/scripts/benchmark-data-parallel-diagnostic.py)
- `max_tokens=128`

Topologies:

- Step 1: `1` worker
- Step 2: `2` replica groups
- Step 3: `3` replica groups
- Step 4: `4` replica groups on `3` physical GPUs with oversubscribe

Three result families were compared:

1. Prior rerun with same prompt and fixed total load:
   [hpc1-qwen25-0.5b-data-parallel-benchmark-rerun-2026-03-28.md](/Users/vladislavhalasiuk/Projects/Repos/comet-node/docs/hpc1-qwen25-0.5b-data-parallel-benchmark-rerun-2026-03-28.md)
2. New run with unique prompts and fixed total load
3. New run with unique prompts and scaled total load

## Results

Primary metric below is aggregate `completion_tokens/sec`.

### Same Prompt, Fixed Total Load

These are the rerun numbers used as the reference line.

| Step | Topology | Completion tok/s |
| --- | --- | ---: |
| 1 | 1 worker | 624.479 |
| 2 | DP x2 | 76.823 |
| 3 | DP x3 | 86.375 |
| 4 | Oversubscribe x4 on 3 GPUs | 85.004 |

### Unique Prompts, Fixed Total Load

Here every request had a unique suffix to reduce shared-prefix effects.

| Step | Topology | Concurrency | Requests | Completion tok/s | Delta vs same-prompt fixed |
| --- | --- | ---: | ---: | ---: | ---: |
| 1 | 1 worker | 12 | 24 | 551.415 | -11.7% |
| 2 | DP x2 | 12 | 24 | 72.256 | -5.9% |
| 3 | DP x3 | 12 | 24 | 73.897 | -14.4% |
| 4 | Oversubscribe x4 on 3 GPUs | 12 | 24 | 83.894 | -1.3% |

### Unique Prompts, Scaled Total Load

Here total concurrency scaled with replica count so each replica saw roughly the same offered load
as the single-worker baseline.

| Step | Topology | Concurrency | Requests | Completion tok/s | Delta vs unique fixed |
| --- | --- | ---: | ---: | ---: | ---: |
| 2 | DP x2 | 24 | 48 | 148.212 | +105.1% |
| 3 | DP x3 | 36 | 72 | 222.745 | +201.4% |
| 4 | Oversubscribe x4 on 3 GPUs | 48 | 96 | 288.479 | +243.9% |

### Still Below Single Worker

Even after both corrections:

- unique prompts
- scaled total load

the DP topologies still did not catch the `1` worker baseline.

Relative to the unique-prompt single-worker result `551.415 completion tok/s`:

- DP x2 scaled reached `26.9%`
- DP x3 scaled reached `40.4%`
- DP x4 scaled reached `52.3%`

## Load Balancer Check

A separate short `DP x2` diagnostic run was executed with `8` unique requests and worker logs were
inspected directly.

Observed result:

- worker `a` logged `4` `POST /v1/chat/completions`
- worker `b` logged `4` `POST /v1/chat/completions`

This confirms that `comet-node` did distribute requests across replica leaders. The main problem is
therefore not “all traffic is accidentally going to one worker”.

## Interpretation

The matrix answers the original hypotheses as follows.

### 1. External internet was not the reason

The benchmarks were executed on `hpc1` against `127.0.0.1:18080`, and the rerun reproduced the
same shape within a small variance band. External internet conditions were not part of the hot
path.

### 2. Prefix caching helped, but it did not explain the collapse

Moving from same prompt to unique prompts reduced the single-worker baseline by about `11.7%`, which
shows that shared-prefix effects were real. But the DP topologies remained dramatically slower even
after removing that advantage.

### 3. Fixed total load was a major confounder

When total concurrency was scaled with the number of replicas, throughput improved strongly:

- `x2`: `72.256 -> 148.212`
- `x3`: `73.897 -> 222.745`
- `x4`: `83.894 -> 288.479`

So the original benchmark did indeed underfeed each replica.

### 4. A larger architectural bottleneck still remains

Even after correcting both major benchmark issues, the best DP result was still only about half of
the single-worker baseline.

That narrows the remaining explanation to runtime architecture effects such as:

- loss of global batching efficiency across independent replicas
- simple round-robin balancing instead of queue-aware or cache-aware balancing
- additional proxy/gateway overhead in the `controller -> infer gateway -> worker` path
- lower per-replica serving efficiency for this small model on this deployment shape

## Current Best Explanation

The original “DP is slower” conclusion was directionally true, but the first benchmark overstated
how bad it was because:

- it kept total load fixed
- it reused the same prompt

After removing those issues, the true picture is:

- DP does scale up when more traffic is offered
- the scale-up is much weaker than expected
- the weak scale-up is not caused by a broken balancer

In other words, `comet-managed replica groups` are functioning, but their aggregate throughput
efficiency on this small-model `hpc1` setup is still poor compared with a single hot `vLLM`
instance.

## Implications

This makes the next engineering target much clearer.

The highest-value next investigations are:

1. Measure direct worker throughput versus `controller -> infer gateway -> worker` throughput.
2. Replace simple round-robin with queue-aware or load-aware balancing.
3. Compare `comet-managed replica groups` against native `vLLM` data-parallel serving on the same
   hardware.
4. Profile per-replica throughput curves versus concurrency for a single worker and for a replica
   group, to see where batching efficiency is being lost.

## Post-Test State

After the diagnostic matrix:

- temporary benchmark planes were stopped and deleted
- `lt-cypher-ai` remained healthy with `ready=true`
- `http://127.0.0.1:18110/cypher/health` remained `status=ok`
