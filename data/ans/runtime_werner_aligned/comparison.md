> Archived alignment-stage results: these measurements use oracle reuse = 1. Current defaults use bounded reuse = 4, cost growth = 0.10; see ../runtime_werner_reuse_full_4/comparison.md.

# Shared-solver purification ablation

ZFA now derives from WernerAlgo2 and sets max_purification_rounds=0; WPFA defaults to 3. Both use exactly the same DP, Z/P bucketing, cache, worker limit, dual update, stopping rule and rounding implementation.

Removed fixed 20-update shape reuse. The oracle is refreshed after every dual update. Both stop at obj >= 1.0. Removed candidate and retained-label hard caps from WPFA rather than adding lossy truncation to ZFA. Bucket approximation remains, with the same precision on both sides.

Normal constructor bucket defaults now match at 0.001; this runtime experiment explicitly uses epsilon=0.9, bucket_eps=0.0001 and one OpenMP thread. The shared epsilon in config.h is unchanged.

## Runtime validation

All three standard main_runtime sweeps completed: 16 points, five fixed graph/request instances, one warmup and three measured repetitions per instance (480 measured samples). Constructors, graph/request/path generation and output are outside the timed region. Native Windows GCC -O3 -march=native; seed=20260820. Inputs: runtime_zfa_equal_round_0..4.input.

ZFA is faster at all 16 points, by 4.25x to 22.18x. This is an observed workload result, not a guarantee for every possible graph. These are newly measured aligned-solver runtimes; do not combine them with old runtimes from another machine or solver policy.

| Sweep | Value | ZFA mean (s) | WPFA mean (s) | WPFA / ZFA |
|---|---:|---:|---:|---:|
| request_cnt | 80 | 0.107458 | 1.266244 | 11.78x |
| request_cnt | 100 | 0.139346 | 1.519008 | 10.90x |
| request_cnt | 120 | 0.167924 | 1.863783 | 11.10x |
| request_cnt | 140 | 0.200279 | 2.101509 | 10.49x |
| request_cnt | 160 | 0.233453 | 2.478129 | 10.62x |
| fidelity_threshold | 0.7 | 0.509259 | 11.293352 | 22.18x |
| fidelity_threshold | 0.75 | 0.316835 | 5.294204 | 16.71x |
| fidelity_threshold | 0.8 | 0.190333 | 2.103241 | 11.05x |
| fidelity_threshold | 0.85 | 0.044573 | 0.412044 | 9.24x |
| fidelity_threshold | 0.9 | 0.004096 | 0.055401 | 13.53x |
| time_limit | 7 | 0.065384 | 0.278050 | 4.25x |
| time_limit | 9 | 0.105057 | 0.737999 | 7.02x |
| time_limit | 11 | 0.145430 | 1.350103 | 9.28x |
| time_limit | 13 | 0.207966 | 2.090951 | 10.05x |
| time_limit | 15 | 0.235525 | 2.797588 | 11.88x |
| time_limit | 17 | 0.268714 | 3.635906 | 13.53x |

## Profiling at 100 requests

| Mode | Mean oracle calls | Mean DP paths | Mean DP time (s) | Mean run time (s) |
|---|---:|---:|---:|---:|
| WPFA-noPurify (ZFA) | 302.6 | 6201.2 | 0.112305 | 0.139346 |
| WPFA | 353.8 | 6678.0 | 1.239047 | 1.519008 |

Oracle and DP times are recorded inside the timed run(). DP time includes workspace preparation and recurrence computation; it excludes terminal scoring/backtracking. With multiple workers DP times are summed worker elapsed times, so they can exceed wall time. These measurements use one worker.

Every measured sample has either one dual update per oracle call or one final empty/failed oracle call. No fixed shape reuse remains. The largest observed frontier contained 31,669 pre-bucket candidates and 27,359 retained labels; it was not truncated.

## Quality and feasibility

The quality check passed 44 old/new solver runs over 11 workloads: all five instances at 100 requests, plus one instance at each request-count/fidelity/time-limit endpoint. Every accepted schedule was independently replayed against fidelity and total node/time memory limits, including extra purification memory. The ZFA wrapper matched direct WernerAlgo2(rounds=0) exactly in all 11 workloads: metrics, CDF, accepted schedules in order, fidelity and success probability. ZFA used zero rounds on every accepted link.

The old and new solvers are intentionally different; this is not a claim that old schedules are preserved. On the five 100-request instances:

| Variant | Mean accepted requests | Mean expected successful requests | Mean fidelity gain |
|---|---:|---:|---:|
| old_ZFA | 60.0 | 51.986994 | 40.073723 |
| ZFA | 60.0 | 51.989509 | 40.089853 |
| old_WPFA | 4.0 | 3.594463 | 2.990599 |
| WPFA | 88.2 | 68.896394 | 52.597114 |

Old WPFA admitted far fewer requests despite using the same epsilon. Alignment changes reuse, truncation and stopping policy together, so this comparison does not isolate the contribution of each change. ZFA also changes from its separate legacy DP to the shared solver; individual schedules and fidelity values may change slightly.

Reference implementation for the quality check: git commit f6c2c31deacfd2f8b078f7071ecdc6cc577eaf63. Old-version profiling fields in werner_quality.csv are 0 because those counters are unavailable, not because the old solver did no work.

## Reproduce

From src, create the output directories first. On Windows add C:/mingw64/bin to PATH for the runtime DLLs.

```powershell
python build_werner_alignment_check.py --runtime
python build_werner_alignment_check.py
$env:PATH='C:\mingw64\bin;'+$env:PATH
./runtime_aligned_probe.exe --algorithms ZFA,ZFA2 --instances 5 --repetitions 3 --warmups 1 --reuse-inputs --input-pattern '../data/input/runtime_zfa_equal_round_{}.input' --output-dir ../data/ans/runtime_werner_aligned
./check_werner_alignment.exe --sweeps request_cnt --request-counts 100 --instances 5 --reuse-inputs --input-pattern '../data/input/runtime_zfa_equal_round_{}.input' --output-dir ../data/ans/runtime_werner_aligned_quality
./check_werner_alignment.exe --request-counts 80,160 --fidelity-thresholds 0.70,0.90 --time-limits 7,17 --instances 1 --reuse-inputs --input-pattern '../data/input/runtime_zfa_equal_round_{}.input' --output-dir ../data/ans/runtime_werner_aligned_edges
```

Without existing inputs, remove --reuse-inputs and add --python python to regenerate the same seeded graphs.

Integration syntax checks passed for main_runtime.cpp, main_small_scale.cpp, main_smallscale.cpp and runtime_compare.cpp. The standard makefile now rebuilds WernerAlgo.o when its shared WernerAlgo2 header changes.

The full seven-algorithm Gurobi main_runtime benchmark was not rerun here. These two-algorithm ANS files are isolated and do not replace the formal fixed-column ANS files or existing plots. Rebuild and rerun main_runtime with Gurobi for the complete publication figures; the WPFA runtime scale has changed.
