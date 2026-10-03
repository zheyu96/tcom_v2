"""Summarize measured WPFA before/after runtime, without a target ordering."""
import argparse
import csv
import statistics
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("directory", type=Path)
args = parser.parse_args()
with (args.directory / "wpfa_fidelity_raw.csv").open(newline="") as f:
    rows = list(csv.DictReader(f))
thresholds = sorted({float(r["threshold"]) for r in rows})
summary = []
for threshold in thresholds:
    groups = {variant: [r for r in rows if float(r["threshold"]) == threshold and r["variant"] == variant]
              for variant in ("before", "after")}
    pairs = {variant: {(r["instance"], r["repetition"]): r for r in group}
             for variant, group in groups.items()}
    if pairs["before"].keys() != pairs["after"].keys():
        raise ValueError("Incomplete before/after pairs")
    for key, before in pairs["before"].items():
        after = pairs["after"][key]
        for metric in ("fidelity_gain", "accepted_schedules", "dp_paths", "peak_candidates", "peak_labels"):
            if before[metric] != after[metric]:
                raise ValueError(f"Mismatch: {threshold} {key} {metric}")
    result = {"threshold": threshold, "pairs": len(pairs["before"])}
    for metric in ("run_seconds", "total_seconds", "paths_seconds", "construction_seconds"):
        for variant, group in groups.items():
            result[f"{variant}_{metric}"] = statistics.mean(float(r[metric]) for r in group)
        result[f"reduction_{metric}_pct"] = 100 * (1 - result[f"after_{metric}"] / result[f"before_{metric}"])
    summary.append(result)
with (args.directory / "wpfa_fidelity_summary.csv").open("w", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=list(summary[0]))
    writer.writeheader()
    writer.writerows(summary)
lines = ["# WPFA fidelity-threshold optimization", "",
         "Generate non-leaf labels directly into reusable per-worker scratch buffers; reuse bucket entries and representative buffers. Original candidate order, bucketing, sorting, epsilon=0.9, bucket_eps=0.0001, oracle reuse=4, and cost growth=0.10 are preserved.", "",
         "Measured on the same seeded inputs: 3 instances, 3 repetitions and 1 warmup per instance/threshold; request count=100, horizon=13, 1 OpenMP thread, GCC -O3 -march=native. Before/after order alternates. Both variants record accepted schedules during timing.", "",
         "The benchmark exits with an error if accepted schedules (in order), memory ranges, purification rounds, fidelity, success probability, expected Werner values, result metrics or CDF differ. The summary also checks identical DP path and peak label/candidate counts. Total time includes fresh path preparation, algorithm construction and run(); graph/request input setup and verification are outside timing.", "",
         f"Timed pairs: {len(rows) // 2}; every pair has identical reported quality and DP workload counts.", "",
         "| Threshold | Before run (s) | After run (s) | Run reduction | Before total (s) | After total (s) | Total reduction |", 
         "|---:|---:|---:|---:|---:|---:|---:|"]
for r in summary:
    lines.append(f"| {r['threshold']:.2f} | {r['before_run_seconds']:.6f} | {r['after_run_seconds']:.6f} | {r['reduction_run_seconds_pct']:.1f}% | {r['before_total_seconds']:.6f} | {r['after_total_seconds']:.6f} | {r['reduction_total_seconds_pct']:.1f}% |")
lines += ["", "The 4-thread exact before/after check failed. A separate --baseline-control run also failed when comparing the original solver with itself on identical input, demonstrating pre-existing parallel nondeterminism. The speed/quality conclusions above are limited to the default single-thread setting. Raw control evidence is in ../wpfa_fidelity_baseline_parallel_control/wpfa_fidelity_raw.csv (both before/after slots run the original solver).", "", "Results apply to these measured workloads. Thread-local scratch retains its peak allocated capacity until the worker thread exits.", "", "Reproduce from src:", "", "```powershell", "python build_wpfa_fidelity_benchmark.py", "$env:PATH='C:\\mingw64\\bin;'+$env:PATH", "./benchmark_wpfa_fidelity.exe --instances 3 --repetitions 3 --warmups 1 --reuse-inputs --input-pattern '../data/input/runtime_zfa_equal_round_{}.input' --output-dir ../data/ans/wpfa_fidelity_optimized --sweeps fidelity_threshold", "python summarize_wpfa_fidelity.py ../data/ans/wpfa_fidelity_optimized", "```", ""]
(args.directory / "comparison.md").write_text("\n".join(lines), encoding="utf-8")
for r in summary:
    print(f"threshold={r['threshold']:.2f} run reduction={r['reduction_run_seconds_pct']:.1f}% total reduction={r['reduction_total_seconds_pct']:.1f}%")
