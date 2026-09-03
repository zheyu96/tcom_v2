#!/usr/bin/env python3
"""Draw one deliberately simple 3-node explanation for the reviewer."""

import argparse
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import patches

from plot_small_scale_reviewer import (
    CASE_LABEL,
    configure_style,
    locate_data_directory,
    read_certificate,
    read_graph,
    read_results,
    save_figure,
)


def parse_args():
    data = locate_data_directory()
    parser = argparse.ArgumentParser(
        description="Create the easy-to-read 3-node optimality figure."
    )
    parser.add_argument(
        "--csv",
        type=Path,
        default=data / "ans" / "main_small_scale_results.csv",
    )
    parser.add_argument(
        "--certificate",
        type=Path,
        default=data / "ans" / "main_small_scale_optimal_schedules.txt",
    )
    parser.add_argument("--graph-dir", type=Path, default=data / "input")
    parser.add_argument("--output-dir", type=Path, default=data / "pdf")
    parser.add_argument("--dpi", type=int, default=300)
    return parser.parse_args()


def draw_network(axis, metadata, edges):
    positions = {0: (0.12, 0.48), 1: (0.50, 0.48), 2: (0.88, 0.48)}
    roles = {0: "Source", 1: "Repeater", 2: "Destination"}

    for left, right, fidelity in edges:
        x_left, y_left = positions[left]
        x_right, y_right = positions[right]
        axis.plot(
            [x_left, x_right], [y_left, y_right],
            color="#505050", linewidth=3, zorder=1,
        )
        axis.text(
            (x_left + x_right) / 2,
            0.60,
            f"Link fidelity = {fidelity:.3f}",
            ha="center",
            va="center",
            fontsize=10,
            bbox={"facecolor": "white", "edgecolor": "none", "pad": 1},
        )

    request_arrow = patches.FancyArrowPatch(
        (positions[0][0], 0.20),
        (positions[2][0], 0.20),
        connectionstyle="arc3,rad=0",
        arrowstyle="-|>",
        mutation_scale=17,
        color="#D62728",
        linewidth=2.2,
        shrinkA=0,
        shrinkB=0,
        zorder=2,
    )
    axis.add_patch(request_arrow)
    axis.text(
        0.50,
        0.07,
        "Three identical requests: node 0 to node 2",
        color="#B2181B",
        ha="center",
        fontsize=11,
        weight="bold",
    )

    for node, (x_value, y_value) in positions.items():
        axis.scatter(
            [x_value], [y_value],
            s=1150,
            marker="o",
            facecolor="#4C78A8",
            edgecolor="black",
            linewidth=1.3,
            zorder=3,
        )
        axis.text(
            x_value, y_value, str(node), color="white",
            ha="center", va="center", fontsize=13, weight="bold", zorder=4,
        )
        axis.text(
            x_value, y_value + 0.16, roles[node],
            ha="center", va="center", fontsize=10, weight="bold",
        )

    axis.text(
        0.50,
        0.96,
        f"5 time slots\n2 memory cells per node\n"
        f"fidelity threshold = {metadata['fidelity_threshold']:.2f}",
        transform=axis.transAxes,
        ha="center",
        va="top",
        fontsize=9.5,
        bbox={
            "boxstyle": "round,pad=0.35",
            "facecolor": "#F3F3F3",
            "edgecolor": "#888888",
        },
    )
    axis.set_title("1. A small network that can be solved exactly", pad=8)
    axis.set_xlim(0, 1)
    axis.set_ylim(-0.03, 1.0)
    axis.axis("off")


def schedule_interval(selection):
    # The source has one memory interval spanning the request's lifetime.
    _source, ranges = selection["ranges"][0]
    return ranges[0]


def draw_schedule(axis, case_certificate):
    selections = sorted(
        case_certificate["selections"], key=lambda item: schedule_interval(item)[0]
    )
    colors = ("#2A9D8F", "#457B9D")
    for row, (selection, color) in enumerate(zip(selections, colors), start=1):
        start, finish = schedule_interval(selection)
        y_value = 3 - row
        axis.barh(
            y_value,
            finish - start + 1,
            left=start - 0.45,
            height=0.58,
            color=color,
            edgecolor="black",
            linewidth=0.8,
            zorder=3,
        )
        path = "-".join(map(str, selection["path"]))
        axis.text(
            (start + finish) / 2,
            y_value,
            f"Accepted: path {path}",
            color="white",
            ha="center",
            va="center",
            fontsize=9,
            weight="bold",
        )

    axis.text(
        2,
        0,
        "Not admitted (no remaining feasible capacity)",
        ha="center",
        va="center",
        color="#666666",
        fontsize=9,
        style="italic",
        bbox={
            "boxstyle": "round,pad=0.35",
            "facecolor": "#EEEEEE",
            "edgecolor": "#999999",
        },
    )
    axis.axvline(2, color="#D62728", linestyle="--", linewidth=1.2, zorder=2)
    axis.annotate(
        "The two accepted requests overlap here;\n"
        "the memory capacity is exactly 2.",
        xy=(2, 1.45),
        xytext=(3.0, 2.42),
        ha="center",
        va="center",
        fontsize=8.5,
        arrowprops={"arrowstyle": "->", "color": "#D62728"},
    )
    axis.set_xlim(-0.55, 4.55)
    axis.set_ylim(-0.65, 2.7)
    axis.set_xticks(range(5))
    axis.set_yticks([2, 1, 0])
    axis.set_yticklabels(["Request 1", "Request 2", "Request 3"])
    axis.set_xlabel("Time slot")
    axis.set_title("2. The exact optimal schedule accepts 2 of 3 requests")
    axis.grid(axis="x", color="#D8D8D8", linewidth=0.7, zorder=0)
    axis.spines["top"].set_visible(False)
    axis.spines["right"].set_visible(False)


def result_card(axis, y_value, title, value, facecolor, textcolor="white"):
    card = patches.FancyBboxPatch(
        (0.08, y_value),
        0.84,
        0.21,
        boxstyle="round,pad=0.025",
        transform=axis.transAxes,
        facecolor=facecolor,
        edgecolor="black",
        linewidth=0.8,
    )
    axis.add_patch(card)
    axis.text(
        0.13, y_value + 0.105, title,
        transform=axis.transAxes,
        ha="left", va="center", fontsize=10, color=textcolor,
    )
    axis.text(
        0.87, y_value + 0.105, value,
        transform=axis.transAxes,
        ha="right", va="center", fontsize=15,
        weight="bold", color=textcolor,
    )


def draw_result(axis, optimum, zfa2):
    axis.axis("off")
    axis.set_title("3. ZFA2 matches the exact optimum", pad=8)
    result_card(
        axis, 0.66, "Exact exhaustive search",
        f"{optimum['expected_werner_sum']:.3f}", "#242424",
    )
    result_card(
        axis, 0.38, "ZFA2",
        f"{zfa2['expected_werner_sum']:.3f}", "#D62728",
    )
    result_card(
        axis, 0.10, "Optimality gap",
        f"{zfa2['optimality_gap_pct']:.2f}%  MATCH", "#2A9D5B",
    )
    axis.text(
        0.5,
        -0.08,
        f"All {optimum['enumerated_schedules']} schedules were enumerated; "
        f"{optimum['feasible_schedules']} were feasible.\n"
        "The exact search used no approximation or time cutoff.",
        transform=axis.transAxes,
        ha="center",
        va="top",
        fontsize=9,
        style="italic",
    )


def main():
    args = parse_args()
    configure_style()
    results = read_results(args.csv)
    certificates = read_certificate(args.certificate)
    case = "line3"
    optimum = results[case]["OPT"]
    zfa2 = results[case]["ZFA2"]
    _offsets, edges = read_graph(
        args.graph_dir / f"small_scale_{case}.input", optimum
    )

    figure = plt.figure(figsize=(10.6, 6.1))
    grid = figure.add_gridspec(
        2, 5, height_ratios=[0.88, 1.12], hspace=0.34, wspace=0.75
    )
    network_axis = figure.add_subplot(grid[0, :])
    schedule_axis = figure.add_subplot(grid[1, 0:3])
    result_axis = figure.add_subplot(grid[1, 3:5])
    draw_network(network_axis, optimum, edges)
    draw_schedule(schedule_axis, certificates[case])
    draw_result(result_axis, optimum, zfa2)
    figure.suptitle(
        "Small-scale validation: does ZFA2 reach the true optimum?",
        fontsize=15,
        weight="bold",
        y=0.985,
    )
    figure.subplots_adjust(left=0.09, right=0.98, bottom=0.12, top=0.91)
    save_figure(
        figure, args.output_dir, "main_small_scale_simple_explanation", args.dpi
    )


if __name__ == "__main__":
    main()
