#!/usr/bin/env python3
"""Draw the small-scale instance: the chain and everything it discloses.

Everything here is read from data/ans/main_small_scale_instance.csv, which
main_small_scale writes straight out of the Graph it ran on, so the figure
cannot drift from the model.  The chain carries its per-link fidelity, Werner
parameter, fiber length and entangling probability; each node carries its
memory capacity and swapping probability; the arrows carry the requests and
their fidelity thresholds.

The default `--layout topology` is the paper figure: the chain alone, since
the batch and decoherence settings are tabulated in
data/ans/<case>_instance_table.tex.  `--layout full` appends the batch strip
and the parameter table for a standalone slide or an appendix.

Output is EPS as well as PDF/PNG, so it drops straight into a LaTeX build.
"""

import argparse
import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import patches

from plot_small_scale_reviewer import (
    configure_style,
    locate_data_directory,
    save_figure,
)
from plot_small_scale_detail import (
    GRIDLINE,
    INK,
    INK_MUTED,
    INK_SECOND,
    PLANE,
    REQUEST_COLORS,
    SURFACE,
)

CASE = "reviewer_line4"
NODE_FILL = "#f0efec"      # nodes are chrome, not a data series


def parse_args():
    data = locate_data_directory()
    parser = argparse.ArgumentParser(
        description="Draw the small-scale instance and its parameters."
    )
    parser.add_argument(
        "--instance",
        type=Path,
        default=data / "ans" / "main_small_scale_instance.csv",
    )
    parser.add_argument("--output-dir", type=Path, default=data / "pdf")
    parser.add_argument("--dpi", type=int, default=300)
    parser.add_argument("--case", default=CASE)
    parser.add_argument(
        "--layout",
        choices=("topology", "full"),
        default="topology",
        help="'topology' draws the chain alone; 'full' appends the batch "
             "strip and the parameter table",
    )
    parser.add_argument(
        "--formats",
        nargs="+",
        default=[".eps", ".pdf", ".png"],
        help="file suffixes to write",
    )
    return parser.parse_args()


def read_instance(filename, case):
    """{'global': {key: value}, 'link': {(a,b): {key: value}}, ...}"""
    instance = {"global": {}, "link": {}, "node": {}, "request": {}}
    with filename.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            if row["case"] != case:
                continue
            value = float(row["value"])
            scope = row["scope"]
            if scope == "global":
                instance["global"][row["key"]] = value
            elif scope == "node":
                node = int(row["node_a"])
                instance["node"].setdefault(node, {})[row["key"]] = value
            else:
                pair = (int(row["node_a"]), int(row["node_b"]))
                instance[scope].setdefault(pair, {})[row["key"]] = value
    if not instance["link"]:
        raise ValueError(
            f"no instance rows for case '{case}'; rerun ./main_small_scale"
        )
    return instance


def chain_order(links, nodes):
    """Node order along the chain, so the drawing follows the fiber."""
    degree = {node: int(values["degree"]) for node, values in nodes.items()}
    endpoints = sorted(node for node, value in degree.items() if value == 1)
    neighbours = {node: [] for node in nodes}
    for left, right in links:
        neighbours[left].append(right)
        neighbours[right].append(left)

    order = [endpoints[0]]
    visited = {endpoints[0]}
    while len(order) < len(nodes):
        nxt = [n for n in neighbours[order[-1]] if n not in visited]
        if not nxt:
            break
        order.append(nxt[0])
        visited.add(nxt[0])
    return order


def draw_topology(axis, instance):
    axis.set_axis_off()
    axis.set_xlim(0, 1)
    axis.set_ylim(0, 1)

    nodes = instance["node"]
    links = instance["link"]
    order = chain_order(links, nodes)
    positions = {
        node: 0.085 + index * (0.83 / max(len(order) - 1, 1))
        for index, node in enumerate(order)
    }
    chain_y = 0.66

    # ---- fibers, with the per-link parameters the model used
    for index in range(len(order) - 1):
        left, right = order[index], order[index + 1]
        values = links.get((left, right)) or links[(right, left)]
        x_left, x_right = positions[left], positions[right]
        axis.plot([x_left, x_right], [chain_y, chain_y],
                  color=INK_SECOND, linewidth=2.0, zorder=1,
                  solid_capstyle="round")
        middle = (x_left + x_right) / 2
        axis.text(
            middle, 0.925,
            f"$F_e = {values['fidelity']:.4f}$      "
            f"$w_e = {values['werner']:.4f}$",
            ha="center", va="center", fontsize=9.6, color=INK,
        )
        axis.text(
            middle, 0.845,
            f"$\\ell = {values['length_km']:.2f}$ km      "
            f"$\\Pr(u,v) = {values['entangle_probability']:.5f}$",
            ha="center", va="center", fontsize=9.6, color=INK_SECOND,
        )
        axis.text(
            middle, 0.745, f"$({left},{right})$",
            ha="center", va="center", fontsize=9.0, color=INK_MUTED,
        )

    # ---- nodes, with their own resources
    for node in order:
        values = nodes[node]
        axis.scatter([positions[node]], [chain_y], s=1150, marker="o",
                     facecolor=NODE_FILL, edgecolor=INK_SECOND,
                     linewidth=1.1, zorder=3)
        axis.text(positions[node], chain_y, f"$v_{node}$", ha="center",
                  va="center", fontsize=12, color=INK, zorder=4)
        axis.text(
            positions[node], 0.545,
            f"$c^t(v_{node}) = {int(values['memory'])}$",
            ha="center", va="center", fontsize=9.4, color=INK,
        )
        axis.text(
            positions[node], 0.475,
            f"$\\Pr(v_{node}) = {values['swap_probability']:.2f}$",
            ha="center", va="center", fontsize=9.4, color=INK_SECOND,
        )

    # ---- the requests this instance has to serve
    requests = sorted(instance["request"])
    arrow_y = (0.315, 0.185, 0.055)
    for index, (source, destination) in enumerate(requests):
        y_value = arrow_y[index % len(arrow_y)]
        colour = REQUEST_COLORS[index % len(REQUEST_COLORS)]
        axis.add_patch(patches.FancyArrowPatch(
            (positions[source], y_value), (positions[destination], y_value),
            arrowstyle="-|>", mutation_scale=13, color=colour,
            linewidth=1.8, shrinkA=0, shrinkB=0, zorder=2,
        ))
        threshold = instance["request"][(source, destination)][
            "fidelity_threshold"]
        axis.text(
            (positions[source] + positions[destination]) / 2,
            y_value + 0.052,
            f"R{index + 1}: $({source},{destination})$,  "
            f"$\\widehat{{F}} = {threshold:.2f}$",
            ha="center", va="bottom", fontsize=9.4, color=INK,
        )


def draw_batch_strip(axis, instance):
    """The batch itself: |T| slots of duration delta, drawn as chrome so the
    time axis of the scheduling figures is concrete here too."""
    values = instance["global"]
    slots = int(values["time_limit"])
    slot_ms = values["slot_duration_s"] * 1e3

    axis.set_axis_off()
    axis.set_xlim(-0.02, 1.02)
    axis.set_ylim(0, 1)

    axis.text(-0.015, 0.55, "Batch $T$:", ha="right", va="center",
              fontsize=9.6, color=INK)
    width = 0.086
    gap = 0.008
    for slot in range(slots):
        left = 0.015 + slot * (width + gap)
        axis.add_patch(patches.Rectangle(
            (left, 0.30), width, 0.50,
            facecolor=PLANE, edgecolor=GRIDLINE, linewidth=0.9,
        ))
        axis.text(left + width / 2, 0.55, f"$t_{slot}$", ha="center",
                  va="center", fontsize=9.4, color=INK_SECOND)
    right_edge = 0.015 + slots * (width + gap)
    axis.text(
        right_edge + 0.010, 0.55,
        rf"$\delta = {slot_ms:.0f}$ ms per slot,  "
        rf"$|T|\,\delta = {slots * slot_ms:.0f}$ ms total "
        rf"($T_{{\mathrm{{mem}}}} = "
        rf"{values['memory_coherence_s'] * 1e3:.0f}$ ms)",
        ha="left", va="center", fontsize=9.4, color=INK_MUTED,
    )


def parameter_rows(instance):
    values = instance["global"]
    slot_ms = values["slot_duration_s"] * 1e3
    coherence_ms = values["memory_coherence_s"] * 1e3
    attempt_ms = values["attempt_duration_s"] * 1e3
    left = [
        ("Nodes / fibers", f"{int(values['nodes'])} / "
                           f"{int(values['edges'])}"),
        ("Requests", f"{int(values['requests'])}"),
        ("Batch length $|T|$", f"{int(values['time_limit'])} slots"),
        ("Slot duration $\\delta$", f"{slot_ms:.0f} ms"),
        ("Memory per node $c^t(v)$", f"{int(values['memory_per_node'])} "
                                     "cells"),
        ("Swap success $\\Pr(v)$", f"{values['swap_probability']:.2f}"),
        ("Fidelity threshold $\\widehat{F}$",
         f"{values['fidelity_threshold']:.2f}"),
        ("Werner threshold $\\widehat{w}$",
         f"{values['werner_threshold']:.4f}"),
    ]
    right = [
        ("Coherence time $T_{\\mathrm{mem}}$", f"{coherence_ms:.0f} ms"),
        ("Decoherence exponent $\\kappa$",
         f"{values['decoherence_kappa']:.0f}"),
        ("Decay per slot $\\eta = \\delta / T_{\\mathrm{mem}}$",
         f"{values['decoherence_eta']:.2f}"),
        ("Fidelity decay $\\Gamma$",
         f"{values['gamma_per_km']:.4f} km$^{{-1}}$"),
        ("Fiber loss $\\lambda$",
         f"{values['lambda_per_km']:.3f} km$^{{-1}}$"),
        ("Attempt duration $\\tau_{\\mathrm{att}}$", f"{attempt_ms:.2f} ms"),
        ("Attempts per slot $\\xi$",
         f"{int(values['entangle_attempts'])}"),
        ("Max pumping rounds", f"{int(values['max_purification_rounds'])}"),
    ]
    return [list(a) + list(b) for a, b in zip(left, right)]


def draw_parameters(axis, instance):
    axis.set_axis_off()
    rows = parameter_rows(instance)
    table = axis.table(
        cellText=rows,
        colLabels=["Parameter", "Value", "Parameter", "Value"],
        cellLoc="left",
        colWidths=[0.30, 0.16, 0.32, 0.16],
        loc="upper center",
    )
    table.auto_set_font_size(False)
    table.set_fontsize(9.0)
    for (row, column), cell in table.get_celld().items():
        cell.set_edgecolor(GRIDLINE)
        cell.set_linewidth(0.6)
        cell.get_text().set_color(INK)
        cell.PAD = 0.04
        if row == 0:
            cell.set_facecolor("#f0efec")
            cell.get_text().set_weight("bold")
        else:
            cell.set_facecolor(SURFACE if row % 2 else PLANE)
        if column in (1, 3):
            cell.get_text().set_color(INK_SECOND)


def main():
    args = parse_args()
    configure_style()
    instance = read_instance(args.instance, args.case)

    full = args.layout == "full"
    figure = plt.figure(figsize=(9.6, 6.9) if full else (9.6, 4.05))
    figure.patch.set_facecolor(SURFACE)

    figure.suptitle(
        "Small-scale validation instance: a 4-node chain with three requests",
        fontsize=13, color=INK, y=0.972 if full else 0.985,
    )
    figure.text(
        0.5, 0.930 if full else 0.912,
        "Link fidelities, fiber lengths, entangling probabilities, node "
        "memories and request thresholds are exactly the values the "
        "simulator ran with.",
        ha="center", va="center", fontsize=9.8, color=INK_SECOND,
        style="italic",
    )

    if full:
        draw_topology(
            figure.add_axes([0.030, 0.430, 0.945, 0.475]), instance)
        draw_batch_strip(
            figure.add_axes([0.135, 0.352, 0.700, 0.060]), instance)
        draw_parameters(
            figure.add_axes([0.055, 0.042, 0.895, 0.285]), instance)
        stem = f"{args.case}_instance"
    else:
        draw_topology(
            figure.add_axes([0.030, 0.030, 0.945, 0.840]), instance)
        stem = f"{args.case}_topology"

    save_figure(figure, args.output_dir, stem, args.dpi,
                suffixes=tuple(args.formats))


if __name__ == "__main__":
    main()
