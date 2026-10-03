# Bounded oracle reuse validation

Both modes use the same solver, epsilon, buckets, threads, graph seeds and requests. Baseline refreshes after every update. The candidate permits at most 4 updates, refreshing earlier when the fixed schedule's alpha + memory dual cost grows more than 10%. This cost guard is a heuristic, not a guarantee on final solution quality.

Measured samples: baseline 160, reuse 160. Preparation and constructors are excluded from run time. Each row below averages paired instances.

| Sweep | Value | Mode | Baseline s | Reuse s | Speedup | Fidelity gain change | Expected success change |
|---|---:|---|---:|---:|---:|---:|---:|
| fidelity_threshold | 0.7 | ZFA | 0.488980 | 0.297799 | 1.64x | +0.031% | +0.049% |
| fidelity_threshold | 0.7 | WPFA | 11.170164 | 6.373344 | 1.75x | +0.031% | +0.049% |
| fidelity_threshold | 0.75 | ZFA | 0.276994 | 0.158423 | 1.75x | -0.033% | +0.001% |
| fidelity_threshold | 0.75 | WPFA | 4.558880 | 2.715331 | 1.68x | +0.100% | +0.129% |
| fidelity_threshold | 0.8 | ZFA | 0.135733 | 0.079483 | 1.71x | -0.165% | -0.150% |
| fidelity_threshold | 0.8 | WPFA | 1.466165 | 0.913868 | 1.60x | +0.259% | +0.217% |
| fidelity_threshold | 0.85 | ZFA | 0.029790 | 0.016708 | 1.78x | +0.019% | +0.022% |
| fidelity_threshold | 0.85 | WPFA | 0.305236 | 0.230367 | 1.32x | +0.959% | +0.963% |
| fidelity_threshold | 0.9 | ZFA | 0.002628 | 0.001860 | 1.41x | +0.000% | +0.000% |
| fidelity_threshold | 0.9 | WPFA | 0.038158 | 0.032841 | 1.16x | +0.205% | +0.215% |
| request_cnt | 80 | ZFA | 0.123838 | 0.055714 | 2.22x | +0.001% | -0.005% |
| request_cnt | 80 | WPFA | 1.438644 | 0.762244 | 1.89x | +0.215% | +0.212% |
| request_cnt | 100 | ZFA | 0.163162 | 0.080299 | 2.03x | -0.165% | -0.150% |
| request_cnt | 100 | WPFA | 1.828518 | 0.933442 | 1.96x | +0.259% | +0.217% |
| request_cnt | 120 | ZFA | 0.176913 | 0.096233 | 1.84x | -0.078% | -0.101% |
| request_cnt | 120 | WPFA | 1.838270 | 1.149354 | 1.60x | -0.411% | -0.400% |
| request_cnt | 140 | ZFA | 0.188194 | 0.108479 | 1.73x | +0.179% | +0.150% |
| request_cnt | 140 | WPFA | 2.071662 | 1.285739 | 1.61x | -0.115% | -0.091% |
| request_cnt | 160 | ZFA | 0.230454 | 0.128942 | 1.79x | -0.192% | -0.193% |
| request_cnt | 160 | WPFA | 2.417556 | 1.506763 | 1.60x | +0.182% | +0.136% |
| time_limit | 7 | ZFA | 0.046464 | 0.030461 | 1.53x | -0.012% | +0.023% |
| time_limit | 7 | WPFA | 0.196352 | 0.126055 | 1.56x | -0.451% | -0.395% |
| time_limit | 9 | ZFA | 0.073837 | 0.043412 | 1.70x | -0.049% | -0.039% |
| time_limit | 9 | WPFA | 0.555918 | 0.343197 | 1.62x | +0.334% | +0.332% |
| time_limit | 11 | ZFA | 0.099638 | 0.066834 | 1.49x | -0.038% | -0.067% |
| time_limit | 11 | WPFA | 0.971117 | 0.604675 | 1.61x | +0.081% | +0.060% |
| time_limit | 13 | ZFA | 0.135321 | 0.072794 | 1.86x | -0.165% | -0.150% |
| time_limit | 13 | WPFA | 1.459736 | 0.899766 | 1.62x | +0.259% | +0.217% |
| time_limit | 15 | ZFA | 0.160848 | 0.085343 | 1.88x | +0.164% | +0.163% |
| time_limit | 15 | WPFA | 2.093825 | 1.255425 | 1.67x | +0.760% | +0.769% |
| time_limit | 17 | ZFA | 0.201002 | 0.105298 | 1.91x | +0.040% | -0.012% |
| time_limit | 17 | WPFA | 2.749209 | 1.680337 | 1.64x | -0.047% | -0.063% |

Negative quality changes mean a loss. The CSV also records individual-instance changes. The 5% screening target concerns measured quality; it is not enforced by the solver.

Worst individual ZFA fidelity_gain change: -1.692% at time_limit=7, instance=4.

Worst individual ZFA succ_request_cnt change: -1.511% at time_limit=7, instance=4.

Worst individual ZFA2 fidelity_gain change: -1.571% at fidelity_threshold=0.85, instance=2.

Worst individual ZFA2 succ_request_cnt change: -1.603% at fidelity_threshold=0.85, instance=2.

Fixed reuse reduces expensive oracle/DP evaluations. It does not improve the worst-case asymptotic complexity: the guard may require an oracle after every update.

These isolated two-algorithm outputs do not replace the formal seven-algorithm Gurobi benchmark.

Feasibility audit: 210 solver runs across 35 workloads passed independent replay of accepted schedules against fidelity and total memory, including purification. The audit also checks exact equivalence between the ZFA wrapper and the shared zero-purification mode.

## Experiment controls and reproduction

Epsilon=0.9, bucket_eps=0.0001, one OpenMP thread; seed=20260820 and input instances runtime_zfa_equal_round_0..4.input. GCC -O3 -march=native, C++17. Each full sweep uses five instances with one measured repetition and no warmup (160 samples per policy). Calibration at 100 requests used five instances, one warmup and three repetitions for each reuse limit 1, 2, 4, 8. Timing varies with machine load; expensive oracle call counts supply a second performance measure.

At 100 requests the maximum-four policy reduces mean oracle calls from 302.6 to 163.6 for ZFA and from 353.8 to 218.6 for WPFA. Limit eight hardly reduces calls further (163.4 / 218.4), so four was selected. Mean fidelity gain changes are -0.165% for ZFA and +0.259% for WPFA; mean accepted counts are 60.0 to 60.0 and 88.2 to 88.6 respectively. FNPR/FLTO retain their existing exact cache optimizations.

From src, with C:/mingw64/bin on PATH:

```powershell
python build_werner_alignment_check.py --runtime
python build_werner_alignment_check.py
foreach ($n in @(1,4)) {
    $out="../data/ans/runtime_werner_reuse_full_$n"
    New-Item -ItemType Directory -Force $out | Out-Null
    ./runtime_aligned_probe.exe --algorithms ZFA,ZFA2 --instances 5 --repetitions 1 --warmups 0 --oracle-reuse $n --reuse-cost-growth 0.10 --reuse-inputs --input-pattern '../data/input/runtime_zfa_equal_round_{}.input' --output-dir $out
}
./check_werner_alignment.exe --request-counts 80,100,160 --fidelity-thresholds 0.70,0.90 --time-limits 7,17 --instances 5 --oracle-reuse 4 --reuse-inputs --input-pattern '../data/input/runtime_zfa_equal_round_{}.input' --output-dir ../data/ans/runtime_werner_reuse_audit
python analyze_werner_reuse.py ../data/ans/runtime_werner_reuse_full_1 ../data/ans/runtime_werner_reuse_full_4 --audit ../data/ans/runtime_werner_reuse_audit
```

The same --oracle-reuse / --reuse-cost-growth controls are exposed through main_runtime. Its default is four / 0.10; --oracle-reuse 1 restores refresh-after-every-update behavior. Shared constructor defaults in config.h apply to other experiment drivers too. Raw and summary output now include reuse controls. No epsilon was changed.

Integration syntax checks passed for main_runtime.cpp, main_small_scale.cpp, main_smallscale.cpp and runtime_compare.cpp. The full seven-algorithm Gurobi experiment was not run. Existing publication charts are unchanged by this task. runtime_comparison.png/pdf are standalone comparisons of the paired runs.
