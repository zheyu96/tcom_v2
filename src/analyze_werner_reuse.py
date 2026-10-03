"""Compare paired main_time runs with and without bounded Werner oracle reuse."""
import argparse
import csv
from collections import defaultdict
from pathlib import Path
from statistics import mean

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("baseline", type=Path)
parser.add_argument("candidate", type=Path)
parser.add_argument("--audit", type=Path, help="Directory containing the completed feasibility audit")
args = parser.parse_args()


def read(directory):
    groups = defaultdict(list)
    with (directory / "main_time_runtime_raw.csv").open() as stream:
        for row in csv.DictReader(stream):
            key = (row["sweep"], float(row["parameter_value"]), row["algorithm"], int(row["instance"]))
            groups[key].append(row)
    return groups


baseline, candidate = read(args.baseline), read(args.candidate)
if not baseline or baseline.keys() != candidate.keys():
    raise SystemExit("Workloads must match and contain measured samples")
controls = {(r["oracle_reuse"], r["reuse_cost_growth"]) for rows in candidate.values() for r in rows}
if len(controls) != 1 or {r["oracle_reuse"] for rows in baseline.values() for r in rows} != {"1"}:
    raise SystemExit("Expected a no-reuse baseline and one consistent candidate policy")
reuse_limit, cost_growth = next(iter(controls))
metrics = ("run_seconds", "fidelity_gain", "succ_request_cnt", "actual_req_cnt", "oracle_calls", "dp_paths")
points = defaultdict(list)
paired = []
for key in sorted(baseline):
    left, right = baseline[key], candidate[key]
    for control in ("epsilon", "bucket_eps", "threads", "request_count", "fidelity_threshold", "time_limit"):
        if {r[control] for r in left} != {r[control] for r in right}:
            raise SystemExit(f"Mismatched {control}: {key}")
    row = dict(zip(("sweep", "parameter_value", "algorithm", "instance"), key))
    for metric in metrics:
        before = mean(float(r[metric]) for r in left)
        after = mean(float(r[metric]) for r in right)
        row[metric + "_baseline"] = before
        row[metric + "_reuse"] = after
        row[metric + "_change_pct"] = 100 * (after / before - 1) if before else (0 if after == 0 else None)
    paired.append(row)
    points[key[:3]].append(row)

with (args.candidate / "paired_comparison.csv").open("w", newline="") as stream:
    writer = csv.DictWriter(stream, fieldnames=list(paired[0]))
    writer.writeheader()
    writer.writerows(paired)

lines = ["# Bounded oracle reuse validation", "",
         "Both modes use the same solver, epsilon, buckets, threads, graph seeds and requests. "
         f"Baseline refreshes after every update. The candidate permits at most {reuse_limit} updates, "
         f"refreshing earlier when the fixed schedule's alpha + memory dual cost grows more than {100 * float(cost_growth):g}%. "
         "This cost guard is a heuristic, not a guarantee on final solution quality.", "",
         f"Measured samples: baseline {sum(map(len, baseline.values()))}, reuse {sum(map(len, candidate.values()))}. "
         "Preparation and constructors are excluded from run time. Each row below averages paired instances.", "",
         "| Sweep | Value | Mode | Baseline s | Reuse s | Speedup | Fidelity gain change | Expected success change |",
         "|---|---:|---|---:|---:|---:|---:|---:|"]
for (sweep, value, algorithm), rows in sorted(points.items()):
    before = mean(r["run_seconds_baseline"] for r in rows)
    after = mean(r["run_seconds_reuse"] for r in rows)
    changes = []
    for metric in ("fidelity_gain", "succ_request_cnt"):
        base = mean(r[metric + "_baseline"] for r in rows)
        changed = mean(r[metric + "_reuse"] for r in rows)
        changes.append(100 * (changed / base - 1) if base else 0)
    lines.append(f"| {sweep} | {value:g} | {'WPFA' if algorithm == 'ZFA2' else 'ZFA'} | "
                 f"{before:.6f} | {after:.6f} | {before / after:.2f}x | {changes[0]:+.3f}% | {changes[1]:+.3f}% |")
lines += ["", "Negative quality changes mean a loss. The CSV also records individual-instance changes. "
          "The 5% screening target concerns measured quality; it is not enforced by the solver.", ""]
for algorithm in ("ZFA", "ZFA2"):
    rows = [r for r in paired if r["algorithm"] == algorithm]
    for metric in ("fidelity_gain", "succ_request_cnt"):
        worst = min(rows, key=lambda r: r[metric + "_change_pct"] or 0)
        lines.append(f"Worst individual {algorithm} {metric} change: {worst[metric + '_change_pct']:+.3f}% "
                     f"at {worst['sweep']}={worst['parameter_value']:g}, instance={worst['instance']}.")
        lines.append("")
lines += ["Fixed reuse reduces expensive oracle/DP evaluations. It does not improve the worst-case "
          "asymptotic complexity: the guard may require an oracle after every update.", "",
          "These isolated two-algorithm outputs do not replace the formal seven-algorithm Gurobi benchmark."]
if args.audit:
    with (args.audit / "werner_quality.csv").open() as stream:
        audits = list(csv.DictReader(stream))
    if not audits or any(r["feasible"] != "1" for r in audits):
        raise SystemExit("Feasibility audit is empty or failed")
    workloads = {(r["sweep"], r["parameter_value"], r["instance"]) for r in audits}
    lines += ["", f"Feasibility audit: {len(audits)} solver runs across {len(workloads)} workloads passed "
              "independent replay of accepted schedules against fidelity and total memory, including purification. "
              "The audit also checks exact equivalence between the ZFA wrapper and the shared zero-purification mode."]
(args.candidate / "comparison.md").write_text("\n".join(lines), encoding="utf-8")
print("\n".join(lines))

# Standalone artifacts; leave the existing publication plots untouched.
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

figure, axes = plt.subplots(1, 3, figsize=(13, 4.2))
for axis, sweep, label in zip(axes, ("request_cnt", "fidelity_threshold", "time_limit"),
                             ("Requests", "Fidelity threshold", "Time limit")):
    for algorithm, color, name in (("ZFA", "tab:red", "ZFA"), ("ZFA2", "tab:blue", "WPFA")):
        values = sorted(value for kind, value, mode in points if kind == sweep and mode == algorithm)
        for policy, style, marker in (("baseline", "--", "o"), ("reuse", "-", "s")):
            times = [mean(r["run_seconds_" + policy] for r in points[sweep, value, algorithm]) for value in values]
            axis.plot(values, times, style, marker=marker, color=color,
                      label=name + (" every update" if policy == "baseline" else " bounded reuse"))
    axis.set_xlabel(label)
    axis.set_ylabel("Mean run time (s)")
    axis.set_yscale("log")
    axis.grid(True, alpha=0.25)
axes[0].legend(fontsize=8)
figure.suptitle("Shared Werner solver: bounded oracle reuse")
figure.tight_layout()
for extension in ("png", "pdf"):
    figure.savefig(args.candidate / ("runtime_comparison." + extension), dpi=180)
plt.close(figure)
