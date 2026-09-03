#!/usr/bin/env python3
"""Draw the single 4-node chain experiment requested by the reviewer."""

import argparse
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import patches

from plot_small_scale_reviewer import (
    configure_style,
    locate_data_directory,
    read_certificate,
    read_graph,
    read_results,
    save_figure,
)


CASE = "reviewer_line4"
REQUEST_COLORS = ("#2A9D8F", "#D97706", "#457B9D")


def parse_args():
    data = locate_data_directory()
    parser = argparse.ArgumentParser(
        description="Plot the canonical 4-node reviewer experiment."
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


def paper_pair(request):
    return request[0] + 1, request[1] + 1


def draw_network(axis, metadata, edges):
    x_positions = {0: 0.08, 1: 0.36, 2: 0.64, 3: 0.92}
    network_y = 0.72

    for left, right, fidelity in edges:
        axis.plot(
            [x_positions[left], x_positions[right]],
            [network_y, network_y],
            color="#4A4A4A",
            linewidth=3,
            zorder=1,
        )
        axis.text(
            (x_positions[left] + x_positions[right]) / 2,
            network_y + 0.11,
            f"F={fidelity:.3f}",
            ha="center",
            va="center",
            fontsize=9.5,
        )

    for node, x_value in x_positions.items():
        axis.scatter(
            [x_value], [network_y],
            s=1050,
            marker="o",
            facecolor="#4C78A8",
            edgecolor="black",
            linewidth=1.2,
            zorder=3,
        )
        axis.text(
            x_value,
            network_y,
            str(node + 1),
            color="white",
            ha="center",
            va="center",
            fontsize=12,
            weight="bold",
            zorder=4,
        )
    request_pairs = [(0, 2), (0, 3), (1, 3)]
    request_y = (0.39, 0.23, 0.07)
    for request_index, ((source, destination), y_value, color) in enumerate(
        zip(request_pairs, request_y, REQUEST_COLORS), start=1
    ):
        arrow = patches.FancyArrowPatch(
            (x_positions[source], y_value),
            (x_positions[destination], y_value),
            arrowstyle="-|>",
            mutation_scale=14,
            color=color,
            linewidth=2.0,
            shrinkA=0,
            shrinkB=0,
        )
        axis.add_patch(arrow)
        source_label, destination_label = paper_pair((source, destination))
        axis.text(
            (x_positions[source] + x_positions[destination]) / 2,
            y_value + 0.045,
            f"R{request_index}: ({source_label},{destination_label})",
            color=color,
            ha="center",
            va="bottom",
            fontsize=9,
            weight="bold",
        )

    axis.set_title(
        "1. Known small instance: a 4-node chain with three requests\n"
        f"T={metadata['time_limit']}, "
        f"memory={metadata['memory_per_node']} per node, "
        f"fidelity threshold={metadata['fidelity_threshold']:.2f}",
        fontsize=12,
        pad=6,
    )
    axis.set_xlim(0, 1)
    axis.set_ylim(-0.03, 0.98)
    axis.axis("off")


def schedule_span(selection):
    starts = []
    finishes = []
    for _node, ranges in selection["ranges"]:
        starts.extend(start for start, _finish in ranges)
        finishes.extend(finish for _start, finish in ranges)
    return min(starts), max(finishes)


def selection_by_request(case_certificate):
    result = {}
    for selection in case_certificate["selections"]:
        source, destination = selection["path"][0], selection["path"][-1]
        result[(source, destination)] = selection
    return result


def draw_exact_schedule(axis, case_certificate):
    request_pairs = [(0, 2), (0, 3), (1, 3)]
    selections = selection_by_request(case_certificate)
    y_positions = (2, 1, 0)

    for request_index, (request, y_value, color) in enumerate(
        zip(request_pairs, y_positions, REQUEST_COLORS), start=1
    ):
        selection = selections[request]
        start, finish = schedule_span(selection)
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
        path = "-".join(str(node + 1) for node in selection["path"])
        axis.text(
            (start + finish) / 2,
            y_value,
            f"Accepted, path {path}",
            color="white",
            ha="center",
            va="center",
            fontsize=8.7,
            weight="bold",
        )

    axis.set_xlim(-0.55, 5.55)
    axis.set_ylim(-0.7, 2.7)
    axis.set_xticks(range(6))
    axis.set_yticks(y_positions)
    axis.set_yticklabels(["R1: (1,3)", "R2: (1,4)", "R3: (2,4)"])
    axis.set_xlabel("Time slot")
    axis.set_title("2. Exact optimum: all three requests are scheduled")
    axis.grid(axis="x", color="#D8D8D8", linewidth=0.7, zorder=0)
    axis.spines["top"].set_visible(False)
    axis.spines["right"].set_visible(False)
    axis.text(
        0.5,
        -0.24,
        "R2 uses one purification round on link (3,4).",
        transform=axis.transAxes,
        ha="center",
        va="top",
        fontsize=9,
        style="italic",
    )


def result_card(axis, y_value, title, value, color):
    rectangle = patches.FancyBboxPatch(
        (0.06, y_value),
        0.88,
        0.21,
        boxstyle="round,pad=0.025",
        transform=axis.transAxes,
        facecolor=color,
        edgecolor="black",
        linewidth=0.9,
    )
    axis.add_patch(rectangle)
    axis.text(
        0.11,
        y_value + 0.105,
        title,
        transform=axis.transAxes,
        color="white",
        ha="left",
        va="center",
        fontsize=10,
    )
    axis.text(
        0.89,
        y_value + 0.105,
        value,
        transform=axis.transAxes,
        color="white",
        ha="right",
        va="center",
        fontsize=14,
        weight="bold",
    )


def draw_result(axis, optimum, wpfa):
    axis.axis("off")
    axis.set_title("3. WPFA stays very close to the true optimum")
    result_card(
        axis,
        0.67,
        "Exact OPT",
        f"{optimum['expected_werner_sum']:.6f}",
        "#242424",
    )
    result_card(
        axis,
        0.39,
        "WPFA",
        f"{wpfa['expected_werner_sum']:.6f}",
        "#D62728",
    )
    result_card(
        axis,
        0.11,
        "Optimality gap",
        f"{wpfa['optimality_gap_pct']:.3f}%",
        "#2A9D5B",
    )
    axis.text(
        0.5,
        -0.02,
        "Both methods accept all 3 requests.\n"
        f"Exact search: {optimum['enumerated_schedules']:,} schedules "
        f"enumerated, {optimum['feasible_schedules']:,} feasible, "
        f"{optimum['search_states']:,} B&B states.\n"
        "No approximation, candidate cap, or time cutoff was used for OPT.",
        transform=axis.transAxes,
        ha="center",
        va="top",
        fontsize=8.7,
        style="italic",
    )


def main():
    args = parse_args()
    configure_style()
    results = read_results(args.csv)
    certificates = read_certificate(args.certificate)
    if CASE not in results or CASE not in certificates:
        raise ValueError("rerun main_small_scale to create reviewer_line4")

    optimum = results[CASE]["OPT"]
    wpfa = results[CASE]["ZFA2"]
    _offsets, edges = read_graph(
        args.graph_dir / "small_scale_reviewer_line4.input", optimum
    )

    figure = plt.figure(figsize=(10.6, 6.2))
    grid = figure.add_gridspec(
        2, 5, height_ratios=[0.95, 1.05], hspace=0.34, wspace=0.75
    )
    network_axis = figure.add_subplot(grid[0, :])
    schedule_axis = figure.add_subplot(grid[1, 0:3])
    result_axis = figure.add_subplot(grid[1, 3:5])
    draw_network(network_axis, optimum, edges)
    draw_exact_schedule(schedule_axis, certificates[CASE])
    draw_result(result_axis, optimum, wpfa)
    figure.suptitle(
        "Small-scale optimality validation of WPFA",
        fontsize=15,
        weight="bold",
        y=0.985,
    )
    figure.subplots_adjust(left=0.10, right=0.98, bottom=0.13, top=0.90)
    save_figure(
        figure, args.output_dir, "reviewer_line4_wpfa_vs_opt", args.dpi
    )


if __name__ == "__main__":
    main()
