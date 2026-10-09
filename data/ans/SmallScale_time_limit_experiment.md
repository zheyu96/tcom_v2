# SmallScale time-limit supplement

Experiment date: 2026-10-09. Driver: `src/main_smallscale.cpp`.

## Design

- Sweep batch horizon `|T| = 2, 3, 4, 5, 6` discrete slots.
- Slot duration: 0.002 s, so horizons represent 4, 6, 8, 10, 12 ms.
- Use the existing deterministic `line3` and `diamond4` topologies, link
  fidelities, candidate path generation and first four requests from each pool.
- Fixed parameters: four requests, memory capacity four per node, fidelity
  threshold 0.80, swapping probability 0.90, WPFA epsilon 0.55, graph bucket
  width 0.01, WPFA bucket width 0.001. Epsilon 0.55 matches the historical CSV;
  the current shared configuration defaults to 0.9 and is not changed.
- Compare exhaustive OPT against the existing `WPFA_forsmall` implementation,
  which includes a WPFA baseline and bounded schedule refinement. These curves
  should be described as the SmallScale variant, rather than plain WPFA alone.
- Every point averages the two topologies, with one sequential execution per
  topology and algorithm. Runtime is a single-run measurement, not a repeated
  benchmark or confidence interval.

Two slots expose infeasible two-hop schedules while retaining feasible one-hop
requests. Three and four slots capture the transition to accepting all four
requests. Five slots include the historical baseline; six slots confirm the
objective plateau. Longer horizons needlessly increase exhaustive enumeration
cost for these particular instances.

## Results

| Slots | Mean OPT objective | Mean WPFA_forsmall objective | Mean accepted requests (both) |
| --- | --- | --- | --- |
| 2 | 1.4048982655 | 1.4048982655 | 1.5 |
| 3 | 2.8841809038 | 2.8841809038 | 3.5 |
| 4 | 3.2579623111 | 3.2579623111 | 4 |
| 5 | 3.2675033410 | 3.2579623111 | 4 |
| 6 | 3.2675033410 | 3.2579623111 | 4 |

All ten OPT runs report `proven_optimal=1`; all WPFA objectives stay below OPT
within floating-point tolerance. Four ANS files were verified against the raw
CSV means. Tiny last-digit floating-point differences are not optimality gaps.

At five slots, OPT matches the historical `request_cnt=4` results for both
topologies. Current-code WPFA_forsmall on diamond4 is 3.2050832989, whereas the
historical CSV contains 3.1669191795, despite the matching recorded parameters.
The origin of this difference has not been established; the supplement records
the fresh run and does not alter historical data. Runtime from this native
Windows build should not be directly compared with older measurements from an
unrecorded build or execution environment.

## Outputs and reproduction

Raw and summary CSVs are `main_smallscale_time_limit_results.csv` and
`main_smallscale_time_limit_summary.csv`. Chart data uses
`SmallScale_time_limit_<metric>.ans` for `fidelity_gain`, `succ_request_cnt`,
`actual_req_cnt` and `runtime`. Columns are horizon, OPT, WPFA. Runtime ANS
values are seconds. EPS and PNG figures are in `data/pdf`, following the existing
ChartGenerator naming convention. Accepted-request plot labels use the existing
display-only division by ten; raw ANS values remain request counts.

From `src`, build and run on the existing make toolchain:

```sh
make main_smallscale
./main_smallscale --sweep time_limit --epsilon 0.55
```

The supplement was actually built with native MinGW g++ using `-O3 -fopenmp
-std=c++17` and the source dependencies listed in the makefile's
`main_smallscale` target. The temporary executable is
`.codex_tmp/main_smallscale_time_limit.exe`; its runtime DLL directory is
`C:/mingw64/bin`. No Gurobi dependency is needed for this target.

From `plot`, regenerate the SmallScale figures with:

```sh
python ChartGenerator.py SmallScale
```

Single-sweep executions now use sweep-specific CSV names to preserve existing
combined experiment records. A full execution still uses the original combined
CSV names.
