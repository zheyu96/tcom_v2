# FNPR / FLTO runtime optimization

Both algorithms preserve the original decisions and use the unchanged shared epsilon=0.9. These changes introduce no approximation, pruning, precision relaxation, or changes to candidate paths and tie-breaking.

FNPR (MyAlgo1): reuse DP buffers, avoid path/shape copies, compute each path probability once per DP evaluation, and cache pair/alpha results until a beta on any candidate-path node changes. Alpha is part of the key.

FLTO (MyAlgo3): use fixed four-state arrays instead of per-cell heap allocations, reuse DP buffers, avoid path/shape copies, and cache each SD pair until a resource changes on any of its candidate paths. Invalidation includes losing candidate paths, not just the last winning shape.

Reference implementation: git commit `0bf285d2cfa677e500e4a6cf76aeb5d3e602c9ad`. The build script extracts and renames the original classes so both variants run in the same binary.

## Validation and timing

The standard three main_runtime sweeps contain 16 parameter points, five seeded graph/request instances and three timed repetitions. Seed=20260820; one OpenMP thread; GCC -O3 -march=native; epsilon=0.9 for both variants. The original and optimized run order alternates. Constructors, graph/request/path generation, result comparison and accepted-schedule recording are outside the timed region.

All 160 full schedule comparisons passed: every accepted schedule in order, node/time memory ranges, purification rounds, fidelity, success probability, expected Werner value, all reported metrics and CDF are identical. This checks 9,450 accepted schedules per variant. All 480 timed result comparisons also passed (960 measured run() calls).

An additional 24 full schedule comparisons and 24 timed result comparisons passed for request counts 1/2/8, time limits 2/3 and fidelity threshold 0.99 on two instances. These cover short horizons, small request sets and infeasible high-threshold schedules.

Results are for these measured workloads; speedup on other graphs can vary. The complete seven-algorithm Gurobi main_runtime benchmark and its existing ANS/PNG files were not overwritten or rerun.

## Mean runtime

| Sweep | Value | Algorithm | Original (s) | Optimized (s) | Speedup |
|---|---:|---|---:|---:|---:|
| request_cnt | 80 | FNPR | 0.357113 | 0.017327 | 20.61x |
| request_cnt | 80 | FLTO | 0.475521 | 0.021895 | 21.72x |
| request_cnt | 100 | FNPR | 0.483955 | 0.023786 | 20.35x |
| request_cnt | 100 | FLTO | 0.728027 | 0.029889 | 24.36x |
| request_cnt | 120 | FNPR | 0.641946 | 0.027352 | 23.47x |
| request_cnt | 120 | FLTO | 1.106851 | 0.036238 | 30.54x |
| request_cnt | 140 | FNPR | 0.811804 | 0.032321 | 25.12x |
| request_cnt | 140 | FLTO | 1.375822 | 0.043983 | 31.28x |
| request_cnt | 160 | FNPR | 0.955546 | 0.036882 | 25.91x |
| request_cnt | 160 | FLTO | 1.812960 | 0.050259 | 36.07x |
| fidelity_threshold | 0.7 | FNPR | 0.485288 | 0.024846 | 19.53x |
| fidelity_threshold | 0.7 | FLTO | 0.837336 | 0.033311 | 25.14x |
| fidelity_threshold | 0.75 | FNPR | 0.493386 | 0.025563 | 19.30x |
| fidelity_threshold | 0.75 | FLTO | 0.829576 | 0.032985 | 25.15x |
| fidelity_threshold | 0.8 | FNPR | 0.487108 | 0.024345 | 20.01x |
| fidelity_threshold | 0.8 | FLTO | 0.756896 | 0.028889 | 26.20x |
| fidelity_threshold | 0.85 | FNPR | 0.503603 | 0.022236 | 22.65x |
| fidelity_threshold | 0.85 | FLTO | 0.243071 | 0.010840 | 22.42x |
| fidelity_threshold | 0.9 | FNPR | 0.516228 | 0.024600 | 20.99x |
| fidelity_threshold | 0.9 | FLTO | 0.038148 | 0.003077 | 12.40x |
| time_limit | 7 | FNPR | 0.333444 | 0.016919 | 19.71x |
| time_limit | 7 | FLTO | 0.511209 | 0.021363 | 23.93x |
| time_limit | 9 | FNPR | 0.375981 | 0.018060 | 20.82x |
| time_limit | 9 | FLTO | 0.555656 | 0.021591 | 25.74x |
| time_limit | 11 | FNPR | 0.542003 | 0.024767 | 21.88x |
| time_limit | 11 | FLTO | 0.830030 | 0.030230 | 27.46x |
| time_limit | 13 | FNPR | 0.584402 | 0.028454 | 20.54x |
| time_limit | 13 | FLTO | 0.838906 | 0.030613 | 27.40x |
| time_limit | 15 | FNPR | 0.659155 | 0.031325 | 21.04x |
| time_limit | 15 | FLTO | 0.915791 | 0.035016 | 26.15x |
| time_limit | 17 | FNPR | 0.639083 | 0.029126 | 21.94x |
| time_limit | 17 | FLTO | 1.022689 | 0.038062 | 26.87x |

## Reproduce

From src:

```powershell
python build_fnpr_flto_benchmark.py
New-Item -ItemType Directory -Force ../data/ans/runtime_fnpr_flto | Out-Null
$env:PATH='C:\mingw64\bin;'+$env:PATH
./benchmark_fnpr_flto.exe --instances 5 --repetitions 3 --warmups 1 --reuse-inputs --input-pattern '../data/input/runtime_zfa_equal_round_{}.input' --output-dir ../data/ans/runtime_fnpr_flto
```

Without existing inputs, remove --reuse-inputs and add --python python to regenerate the same seeded graphs. On Linux, omit the PowerShell PATH setup and create the output directory with mkdir -p.

Outputs: fnpr_flto_raw.csv, fnpr_flto_summary.csv, fnpr_flto_quality.csv. Quality CSV values are 1 only after exact schedule and metric checks pass; a mismatch stops the benchmark with a nonzero exit code.

Integration syntax checks passed for main_runtime.cpp, main_small_scale.cpp and main_smallscale.cpp. Rebuild the normal experiment executable to use the optimized algorithms.
