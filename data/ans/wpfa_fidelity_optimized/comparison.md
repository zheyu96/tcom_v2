# WPFA fidelity-threshold optimization

Generate non-leaf labels directly into reusable per-worker scratch buffers; reuse bucket entries and representative buffers. Original candidate order, bucketing, sorting, epsilon=0.9, bucket_eps=0.0001, oracle reuse=4, and cost growth=0.10 are preserved.

Measured on the same seeded inputs: 3 instances, 3 repetitions and 1 warmup per instance/threshold; request count=100, horizon=13, 1 OpenMP thread, GCC -O3 -march=native. Before/after order alternates. Both variants record accepted schedules during timing.

The benchmark exits with an error if accepted schedules (in order), memory ranges, purification rounds, fidelity, success probability, expected Werner values, result metrics or CDF differ. The summary also checks identical DP path and peak label/candidate counts. Total time includes fresh path preparation, algorithm construction and run(); graph/request input setup and verification are outside timing.

Timed pairs: 45; every pair has identical reported quality and DP workload counts.

| Threshold | Before run (s) | After run (s) | Run reduction | Before total (s) | After total (s) | Total reduction |
|---:|---:|---:|---:|---:|---:|---:|
| 0.70 | 4.687406 | 4.441191 | 5.3% | 4.701888 | 4.455280 | 5.2% |
| 0.75 | 2.427103 | 2.268386 | 6.5% | 2.442641 | 2.284873 | 6.5% |
| 0.80 | 0.912241 | 0.814441 | 10.7% | 0.928198 | 0.828268 | 10.8% |
| 0.85 | 0.241487 | 0.215197 | 10.9% | 0.255520 | 0.230623 | 9.7% |
| 0.90 | 0.026893 | 0.022384 | 16.8% | 0.040515 | 0.035921 | 11.3% |

The 4-thread exact before/after check failed. A separate --baseline-control run also failed when comparing the original solver with itself on identical input, demonstrating pre-existing parallel nondeterminism. The speed/quality conclusions above are limited to the default single-thread setting. Raw control evidence is in ../wpfa_fidelity_baseline_parallel_control/wpfa_fidelity_raw.csv (both before/after slots run the original solver).

Results apply to these measured workloads. Thread-local scratch retains its peak allocated capacity until the worker thread exits.

Reproduce from src:

```powershell
python build_wpfa_fidelity_benchmark.py
$env:PATH='C:\mingw64\bin;'+$env:PATH
./benchmark_wpfa_fidelity.exe --instances 3 --repetitions 3 --warmups 1 --reuse-inputs --input-pattern '../data/input/runtime_zfa_equal_round_{}.input' --output-dir ../data/ans/wpfa_fidelity_optimized --sweeps fidelity_threshold
python summarize_wpfa_fidelity.py ../data/ans/wpfa_fidelity_optimized
```
