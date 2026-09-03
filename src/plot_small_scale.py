#!/usr/bin/env python3
"""Create the publication figure for main_small_scale.cpp results."""

import argparse
import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import PercentFormatter


ALGORITHM_ORDER = ["OPT", "ZFA2", "ZFA", "MyAlgo1", "MyAlgo3"]
ALGORITHM_STYLE = {
    "OPT": {"color": "#242424", "hatch": "///"},
    "ZFA2": {"color": "#D62728", "hatch": ""},
    "ZFA": {"color": "#1F77B4", "hatch": ""},
    "MyAlgo1": {"color": "#7F7F7F", "hatch": ""},
    "MyAlgo3": {"color": "#2CA02C", "hatch": ""},
}
CASE_LABELS = {
    "line3": "3-node\nline",
    "line4": "4-node\nline",
    "diamond4": "4-node\ndiamond",
}


def default_input_path():
    candidates = [
        Path("../data/ans/main_small_scale_results.csv"),
        Path("data/ans/main_small_scale_results.csv"),
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return candidates[0]


def parse_args():
    parser = argparse.ArgumentParser(
        description=(
            "Plot exact objective values and optimality gaps from the "
            "3/4-node experiment."
        )
    )
    parser.add_argument(
        "--input",
        type=Path,
        default=default_input_path(),
        help="main_small_scale_results.csv",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        help="output directory (default: the repository data/pdf directory)",
    )
    parser.add_argument(
        "--stem",
        default="main_small_scale_optimality",
        help="output filename without extension",
    )
    parser.add_argument("--dpi", type=int, default=300)
    return parser.parse_args()


def read_results(filename):
    required = {
        "case",
        "algorithm",
        "expected_werner_sum",
        "optimality_gap_pct",
    }
    with filename.open(newline="", encoding="utf-8") as source:
        reader = csv.DictReader(source)
        missing = required.difference(reader.fieldnames or [])
        if missing:
            raise ValueError(
                "small-scale CSV is missing: " + ", ".join(sorted(missing))
            )
        rows = []
        for row in reader:
            if row["algorithm"] not in ALGORITHM_ORDER:
                continue
            rows.append(
                {
                    "case": row["case"],
                    "algorithm": row["algorithm"],
                    "objective": float(row["expected_werner_sum"]),
                    "gap": float(row["optimality_gap_pct"]),
                }
            )
    if not rows:
        raise ValueError("the small-scale CSV contains no recognized rows")
    return rows


def validate_and_index(rows):
    table = {}
    case_order = []
    for row in rows:
        case = row["case"]
        if case not in table:
            table[case] = {}
            case_order.append(case)
        if row["algorithm"] in table[case]:
            raise ValueError(
                f"duplicate {row['algorithm']} row for case {case}"
            )
        table[case][row["algorithm"]] = row

    for case in case_order:
        if "OPT" not in table[case]:
            raise ValueError(f"case {case} has no exact OPT row")
    algorithms = [
        algorithm
        for algorithm in ALGORITHM_ORDER
        if any(algorithm in table[case] for case in case_order)
    ]
    return table, case_order, algorithms


def configure_plot_style():
    matplotlib.rcParams.update(
        {
            "font.family": "serif",
            "font.serif": ["Times New Roman", "Times", "DejaVu Serif"],
            "font.size": 10,
            "axes.labelsize": 11,
            "axes.titlesize": 11,
            "legend.fontsize": 9,
            "xtick.labelsize": 10,
            "ytick.labelsize": 10,
            "pdf.fonttype": 42,
            "ps.fonttype": 42,
            "axes.unicode_minus": False,
        }
    )


def draw_grouped_bars(axis, table, cases, algorithms, metric):
    positions = list(range(len(cases)))
    group_width = 0.82
    bar_width = group_width / len(algorithms)
    for algorithm_index, algorithm in enumerate(algorithms):
        offset = (
            algorithm_index - (len(algorithms) - 1) / 2.0
        ) * bar_width
        values = [
            table[case].get(algorithm, {}).get(metric, 0.0)
            for case in cases
        ]
        style = ALGORITHM_STYLE[algorithm]
        bars = axis.bar(
            [position + offset for position in positions],
            values,
            width=bar_width * 0.92,
            label=algorithm,
            color=style["color"],
            hatch=style["hatch"],
            edgecolor="black",
            linewidth=0.55,
            zorder=3,
        )
        if metric == "gap":
            for bar, value in zip(bars, values):
                # Several methods can tie OPT at zero.  Label only ZFA2's
                # zero-gap bars (the proposed method) to avoid overlapping
                # text at the baseline while retaining the reviewer-facing
                # result.
                if value <= 0.005 and algorithm != "ZFA2":
                    continue
                label_y = value + (1.8 if value > 0 else 1.0)
                axis.text(
                    bar.get_x() + bar.get_width() / 2,
                    label_y,
                    f"{value:.2f}%" if value >= 0.005 and value < 10
                    else (f"{value:.0f}%" if value >= 10 else "0%"),
                    ha="center",
                    va="bottom",
                    fontsize=7.5,
                    rotation=90 if value >= 10 else 0,
                )
    axis.set_xticks(positions)
    axis.set_xticklabels([CASE_LABELS.get(case, case) for case in cases])
    axis.grid(axis="y", color="#D8D8D8", linewidth=0.7, zorder=0)
    axis.spines["top"].set_visible(False)
    axis.spines["right"].set_visible(False)


def create_figure(rows, output_directory, stem, dpi):
    table, cases, algorithms = validate_and_index(rows)
    configure_plot_style()
    figure, axes = plt.subplots(1, 2, figsize=(10.0, 3.8))

    draw_grouped_bars(
        axes[0], table, cases, algorithms, metric="objective"
    )
    axes[0].set_ylabel("Expected Werner-parameter sum")
    axes[0].set_title("(a) Objective value versus exact optimum")
    axes[0].set_ylim(bottom=0)
    axes[0].legend(
        frameon=False,
        ncol=len(algorithms),
        loc="upper center",
        bbox_to_anchor=(1.05, 1.23),
        columnspacing=1.15,
        handlelength=1.5,
    )

    approximate_algorithms = [
        algorithm for algorithm in algorithms if algorithm != "OPT"
    ]
    draw_grouped_bars(
        axes[1], table, cases, approximate_algorithms, metric="gap"
    )
    axes[1].set_ylabel("Optimality gap (%)")
    axes[1].set_title("(b) Gap to exhaustive optimum (lower is better)")
    axes[1].set_ylim(0, 112)
    axes[1].yaxis.set_major_formatter(PercentFormatter(xmax=100, decimals=0))

    figure.subplots_adjust(left=0.08, right=0.985, bottom=0.19, top=0.79, wspace=0.28)
    output_directory.mkdir(parents=True, exist_ok=True)
    outputs = [
        output_directory / f"{stem}.pdf",
        output_directory / f"{stem}.png",
    ]
    for output in outputs:
        save_options = {"bbox_inches": "tight"}
        if output.suffix == ".png":
            save_options["dpi"] = dpi
        figure.savefig(output, **save_options)
        print(f"saved {output.resolve()}")
    plt.close(figure)


def main():
    args = parse_args()
    output_directory = args.output_dir
    if output_directory is None:
        # .../data/ans/results.csv -> .../data/pdf
        output_directory = args.input.resolve().parent.parent / "pdf"
    create_figure(
        read_results(args.input), output_directory, args.stem, args.dpi
    )


if __name__ == "__main__":
    main()
