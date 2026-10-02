# ZFA / WPFA runtime comparison

Fixed approximation epsilon is now defined once in src/config.h: EXPERIMENT_EPSILON = 0.9. Explicit epsilon sensitivity sweeps retain their scanned values.

Measured with the shared main_time.cpp implementation used by main_runtime.cpp. This Windows probe includes ZFA and ZFA2 only; it does not replace the full seven-algorithm Gurobi benchmark or its existing ANS files.

Same seeded graphs and requests, epsilon=0.9, bucket_eps=0.0001, one OpenMP thread, five instances, three measured repetitions and one warmup per point. Seed: 20260820. Timed region: run() only.

| Sweep | Value | ZFA mean (s) | WPFA mean (s) | ZFA / WPFA |
|---|---:|---:|---:|---:|
| request_cnt | 80 | 0.078000 | 0.029628 | 2.633 |
| request_cnt | 100 | 0.095234 | 0.031799 | 2.995 |
| request_cnt | 120 | 0.116880 | 0.034628 | 3.375 |
| request_cnt | 140 | 0.125930 | 0.038208 | 3.296 |
| request_cnt | 160 | 0.157994 | 0.040295 | 3.921 |
| fidelity_threshold | 0.7 | 0.135189 | 0.159046 | 0.850 |
| fidelity_threshold | 0.75 | 0.120098 | 0.085643 | 1.402 |
| fidelity_threshold | 0.8 | 0.089003 | 0.030319 | 2.936 |
| fidelity_threshold | 0.85 | 0.024073 | 0.008876 | 2.712 |
| fidelity_threshold | 0.9 | 0.002576 | 0.003276 | 0.786 |
| time_limit | 7 | 0.035731 | 0.005295 | 6.749 |
| time_limit | 9 | 0.051447 | 0.013063 | 3.938 |
| time_limit | 11 | 0.074585 | 0.021112 | 3.533 |
| time_limit | 13 | 0.089912 | 0.030235 | 2.974 |
| time_limit | 15 | 0.104730 | 0.040273 | 2.601 |
| time_limit | 17 | 0.123984 | 0.051449 | 2.410 |

ZFA is faster at fidelity thresholds 0.70 and 0.90; it remains slower at the other 14 points. This experiment does not establish universal superiority or unchanged solution quality. Changing epsilon changes approximation precision.

Reproduce from src after building main_time.cpp as runtime_zfa_probe.exe:

```powershell
$env:PATH='C:\mingw64\bin;'+$env:PATH
./runtime_zfa_probe.exe --algorithms ZFA,ZFA2 --instances 5 --repetitions 3 --warmups 1 --python python --input-pattern ../data/input/runtime_zfa_equal_round_{}.input --output-dir ../data/ans/runtime_zfa_equal
```

Validation: benchmark compiled and all 480 timed samples completed. Syntax checks passed for main_runtime.cpp, main_small_scale.cpp, main_smallscale.cpp and runtime_compare.cpp. Native Windows main.cpp compilation is unavailable because that existing driver requires sys/resource.h. Full Gurobi baselines were not rerun.

## Original ZFA settings on the same inputs

Original ZFA: epsilon=0.35, bucket_eps=0.01. New ZFA: epsilon=0.9, bucket_eps=0.0001. All 240 original-setting timed samples completed. This comparison changes both parameters; it does not isolate the effect of epsilon. See comparison.csv for all points.

| Sweep | Value | Original ZFA (s) | New ZFA (s) | Speedup |
|---|---:|---:|---:|---:|
| request_cnt | 80 | 0.701515 | 0.078000 | 8.99x |
| request_cnt | 100 | 0.991058 | 0.095234 | 10.41x |
| request_cnt | 120 | 1.314949 | 0.116880 | 11.25x |
| request_cnt | 140 | 1.616857 | 0.125930 | 12.84x |
| request_cnt | 160 | 2.018349 | 0.157994 | 12.77x |
| fidelity_threshold | 0.7 | 1.903687 | 0.135189 | 14.08x |
| fidelity_threshold | 0.75 | 1.632769 | 0.120098 | 13.60x |
| fidelity_threshold | 0.8 | 0.983637 | 0.089003 | 11.05x |
| fidelity_threshold | 0.85 | 0.194664 | 0.024073 | 8.09x |
| fidelity_threshold | 0.9 | 0.009866 | 0.002576 | 3.83x |
| time_limit | 7 | 0.478457 | 0.035731 | 13.39x |
| time_limit | 9 | 0.663006 | 0.051447 | 12.89x |
| time_limit | 11 | 0.814500 | 0.074585 | 10.92x |
| time_limit | 13 | 1.006577 | 0.089912 | 11.20x |
| time_limit | 15 | 1.146739 | 0.104730 | 10.95x |
| time_limit | 17 | 1.327109 | 0.123984 | 10.70x |
