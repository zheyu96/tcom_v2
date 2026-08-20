#!/usr/bin/env python3
"""Plot WPFA epsilon/bucket_eps sweeps from runtime_compare summary CSV."""

import argparse
import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


SERIES_STYLE = {
    "bucket_eps": {"color": "red", "marker": "s", "linestyle": "-"},
    "epsilon": {"color": "blue", "marker": "o", "linestyle": "--"},
}


def parse_args():
    parser = argparse.ArgumentParser(
        description="Create the two-panel WPFA approximation-parameter figure."
    )
    parser.add_argument(
        "--input",
        default="../data/ans/wpfa_parameter_sweep_summary.csv",
        help="runtime_compare summary CSV",
    )
    parser.add_argument(
        "--output",
        default="../data/ans/wpfa_parameter_sweep.png",
        help="output image path",
    )
    parser.add_argument(
        "--time-limit",
        type=int,
        help="plot only this time limit; otherwise plot every value found",
    )
    parser.add_argument("--dpi", type=int, default=300)
    return parser.parse_args()


def read_rows(filename):
    required = {
        "time_limit",
        "algorithm",
        "sweep_parameter",
        "parameter_value",
        "mean_seconds",
        "mean_expected_werner_sum",
        "mean_actual_requests",
    }
    with open(filename, newline="", encoding="utf-8") as source:
        reader = csv.DictReader(source)
        missing = required.difference(reader.fieldnames or [])
        if missing:
            raise ValueError("summary CSV is missing: " + ", ".join(sorted(missing)))

        rows = []
        for row in reader:
            if row["algorithm"] != "WPFA":
                continue
            if row["sweep_parameter"] not in SERIES_STYLE:
                continue
            rows.append(
                {
                    "time_limit": int(row["time_limit"]),
                    "parameter": row["sweep_parameter"],
                    "value": float(row["parameter_value"]),
                    "runtime": float(row["mean_seconds"]),
                    "werner_sum": float(row["mean_expected_werner_sum"]),
                    "accepted": float(row["mean_actual_requests"]),
                }
            )
    if not rows:
        raise ValueError("no WPFA parameter-sweep rows found")
    return rows


def output_for_time(base_output, time_limit, multiple):
    path = Path(base_output)
    if not multiple:
        return path
    return path.with_name(f"{path.stem}_T{time_limit}{path.suffix}")


def plot_time_limit(rows, time_limit, output, dpi):
    selected = [row for row in rows if row["time_limit"] == time_limit]
    fig, axes = plt.subplots(1, 2, figsize=(9.2, 3.8))

    metrics = (
        ("werner_sum", "Expected Werner-parameter sum",
         "(a) Runtime vs expected Werner sum"),
        ("accepted", "# Accepted requests",
         "(b) Runtime vs accepted requests"),
    )
    for axis, (metric, ylabel, title) in zip(axes, metrics):
        for parameter in ("bucket_eps", "epsilon"):
            series = sorted(
                (row for row in selected if row["parameter"] == parameter),
                key=lambda row: row["value"],
            )
            if not series:
                continue
            style = SERIES_STYLE[parameter]
            axis.plot(
                [row["runtime"] for row in series],
                [row[metric] for row in series],
                label=parameter,
                markerfacecolor="white",
                linewidth=1.3,
                markersize=6,
                **style,
            )
            for row in series:
                axis.annotate(
                    f'{row["value"]:g}',
                    (row["runtime"], row[metric]),
                    xytext=(3, -11),
                    textcoords="offset points",
                    color=style["color"],
                    fontsize=8,
                )

        axis.set_xlabel("Runtime (seconds)")
        axis.set_ylabel(ylabel)
        axis.set_title(title)
        axis.grid(alpha=0.2)
        axis.legend(frameon=False)

    fig.suptitle(f"WPFA approximation parameters, time limit = {time_limit}")
    fig.tight_layout()
    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output, dpi=dpi, bbox_inches="tight")
    plt.close(fig)
    print(f"saved {output}")


def main():
    args = parse_args()
    rows = read_rows(args.input)
    time_limits = sorted({row["time_limit"] for row in rows})
    if args.time_limit is not None:
        if args.time_limit not in time_limits:
            raise ValueError(f"time limit {args.time_limit} is not present in the CSV")
        time_limits = [args.time_limit]

    multiple = len(time_limits) > 1
    for time_limit in time_limits:
        plot_time_limit(
            rows,
            time_limit,
            output_for_time(args.output, time_limit, multiple),
            args.dpi,
        )


if __name__ == "__main__":
    main()
