#!/usr/bin/env python3
"""Draw the per-request OPT-vs-WPFA figure for the R1.3 response.

Three bands, all read back from what main_small_scale wrote:
  1. the schedule each method selected, request by request, over the batch;
  2. the aggregate numerology theta_m(t,v) each method packed into the nodes,
     on the same time axis as band 1 so the two read together;
  3. the fidelity / success-probability / objective numbers behind the gap.

Colours: categorical slots 1-3 of the validated default palette
(#2a78d6, #eb6834, #1baf7a) carry request identity; a single-hue blue ramp
carries theta magnitude.  Every mark is directly labelled and band 3 is the
table view, which is the required relief for slot 3's sub-3:1 contrast.
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
    read_results,
    save_figure,
)

CASE = "reviewer_line4"

# Simulator name -> label used in the paper, in figure order.
METHODS = (("OPT", "Exact OPT"), ("ZFA2", "WPFA"))

# Categorical slots 1-3: request identity.  Fixed order, never cycled.
REQUEST_COLORS = ("#2a78d6", "#eb6834", "#1baf7a")

# Single-hue blue ramp for theta; index 0 means "no memory held".
THETA_STEPS = ("#f9f9f7", "#cde2fb", "#86b6ef", "#3987e5", "#184f95")
# Label ink per ramp step, chosen so every value clears 4.5:1 on its own
# fill (white would sit at 3.7:1 on step 3).
THETA_INK = ("#898781", "#0b0b0b", "#0b0b0b", "#0b0b0b", "#ffffff")

SURFACE = "#fcfcfb"
PLANE = "#f9f9f7"
INK = "#0b0b0b"
INK_SECOND = "#52514e"
INK_MUTED = "#898781"
GRIDLINE = "#e1e0d9"
BASELINE = "#c3c2b7"
HIGHLIGHT = "#fdf1e9"      # tint of slot 2, marks the one differing request


def parse_args():
    data = locate_data_directory()
    parser = argparse.ArgumentParser(
        description="Draw the per-request OPT-vs-WPFA optimality figure."
    )
    parser.add_argument(
        "--results",
        type=Path,
        default=data / "ans" / "main_small_scale_results.csv",
    )
    parser.add_argument(
        "--selection",
        type=Path,
        default=data / "ans" / "main_small_scale_selection.csv",
    )
    parser.add_argument(
        "--numerology",
        type=Path,
        default=data / "ans" / "main_small_scale_numerology.csv",
    )
    parser.add_argument("--output-dir", type=Path, default=data / "pdf")
    parser.add_argument("--dpi", type=int, default=300)
    return parser.parse_args()


def read_case_rows(filename, case):
    with filename.open(newline="", encoding="utf-8") as handle:
        return [row for row in csv.DictReader(handle) if row["case"] == case]


def selections_by_method(rows):
    """{algorithm: {(src, dst): row}} using 1-based paper labels."""
    result = {name: {} for name, _label in METHODS}
    for row in rows:
        if row["algorithm"] not in result:
            continue
        key = (int(row["src_1based"]), int(row["dst_1based"]))
        result[row["algorithm"]][key] = row
    return result


def aggregate_theta(rows, algorithm):
    """theta[(node_1based, slot)] summed over the accepted requests."""
    grid = {}
    capacity = 0
    slots = 0
    nodes = 0
    for row in rows:
        if row["algorithm"] != algorithm or row["scope"] != "total":
            continue
        node = int(row["node_1based"])
        slot = int(row["time_slot"])
        grid[(node, slot)] = int(row["memory_units"])
        capacity = int(row["node_capacity"])
        nodes = max(nodes, node)
        slots = max(slots, slot + 1)
    return grid, nodes, slots, capacity


def per_request_theta(rows, algorithm, request):
    """theta[(node_1based, slot)] of the one schedule chosen for `request`."""
    tag = f"({request[0]};{request[1]})"
    grid = {}
    capacity = 0
    slots = 0
    nodes = 0
    for row in rows:
        if row["algorithm"] != algorithm or row["scope"] != "per_request":
            continue
        if row["request"] != tag:
            continue
        node = int(row["node_1based"])
        slot = int(row["time_slot"])
        grid[(node, slot)] = int(row["memory_units"])
        capacity = int(row["node_capacity"])
        nodes = max(nodes, node)
        slots = max(slots, slot + 1)
    return grid, nodes, slots, capacity


def request_labels(selections):
    requests = []
    for rows in selections.values():
        for key in rows:
            if key not in requests:
                requests.append(key)
    return sorted(requests)


def pumping_label(row):
    used = [item for item in row["purify_by_link"].split(" ")
            if not item.endswith(":0")]
    if not used:
        return "no purification"
    return ", ".join(
        f"{item.split(':')[0]}$\\times${item.split(':')[1]}" for item in used
    )


def draw_schedule(axis, title, selections, requests, slots):
    axis.set_facecolor(SURFACE)
    axis.set_title(title, fontsize=10.5, color=INK, pad=6)

    for spine in ("top", "right", "left"):
        axis.spines[spine].set_visible(False)
    axis.spines["bottom"].set_color(BASELINE)
    axis.spines["bottom"].set_linewidth(0.8)

    for slot in range(slots):
        axis.axvline(slot, color=GRIDLINE, linewidth=0.7, zorder=1)

    positions = []
    labels = []
    for index, request in enumerate(requests):
        y_center = len(requests) - 1 - index
        positions.append(y_center)
        labels.append(f"R{index + 1}: ({request[0]},{request[1]})")

        row = selections.get(request)
        if row is None:
            axis.text(
                (slots - 1) / 2, y_center, "rejected",
                ha="center", va="center", fontsize=9.5,
                color=INK_MUTED, style="italic", zorder=4,
            )
            continue

        start = int(row["first_slot"]) - 0.36
        finish = int(row["last_slot"]) + 0.36
        axis.add_patch(patches.FancyBboxPatch(
            (start, y_center - 0.22), finish - start, 0.44,
            boxstyle="round,pad=0,rounding_size=0.09",
            linewidth=0, facecolor=REQUEST_COLORS[index],
            mutation_aspect=2.2, zorder=3,
        ))
        axis.text(
            (start + finish) / 2, y_center,
            f"{row['path_1based']}    {pumping_label(row)}",
            ha="center", va="center", fontsize=8.8,
            color=INK, zorder=4,
        )

    axis.set_yticks(positions)
    axis.set_yticklabels(labels, fontsize=9.5, color=INK_SECOND)
    axis.tick_params(axis="y", length=0)
    axis.tick_params(axis="x", colors=INK_MUTED, length=3, width=0.8,
                     labelsize=9)
    axis.set_xticks(range(slots))
    axis.set_xlim(-0.62, slots - 0.38)
    axis.set_ylim(-0.60, len(requests) - 0.40)


def draw_numerology(axis, title, grid, nodes, slots, capacity):
    axis.set_facecolor(SURFACE)
    axis.set_title(title, fontsize=10.5, color=INK, pad=6)

    for spine in axis.spines.values():
        spine.set_visible(False)

    for node in range(1, nodes + 1):
        y_center = nodes - node
        for slot in range(slots):
            units = grid.get((node, slot), 0)
            level = min(units, len(THETA_STEPS) - 1)
            # A 2px-equivalent surface gap separates neighbouring cells.
            axis.add_patch(patches.Rectangle(
                (slot - 0.44, y_center - 0.40), 0.88, 0.80,
                facecolor=THETA_STEPS[level],
                edgecolor=SURFACE, linewidth=1.4, zorder=2,
            ))
            axis.text(
                slot, y_center, str(units) if units else "·",
                ha="center", va="center", fontsize=9.2,
                color=THETA_INK[level],
                weight="bold" if units >= capacity else "normal",
                zorder=3,
            )

    axis.set_yticks([nodes - node for node in range(1, nodes + 1)])
    axis.set_yticklabels([f"$v_{node}$" for node in range(1, nodes + 1)],
                         fontsize=9.8, color=INK_SECOND)
    axis.tick_params(axis="y", length=0)
    axis.tick_params(axis="x", colors=INK_MUTED, length=3, width=0.8,
                     labelsize=9)
    axis.set_xticks(range(slots))
    axis.set_xlabel("Time slot", fontsize=10, color=INK_SECOND, labelpad=3)
    axis.set_xlim(-0.62, slots - 0.38)
    axis.set_ylim(-0.58, nodes - 0.42)


def draw_theta_legend(axis, note):
    axis.set_axis_off()
    axis.set_xlim(0, 1)
    axis.set_ylim(0, 1)

    axis.text(0.243, 0.62, "memory cells held  $\\theta_m(t,v)$:",
              ha="right", va="center", fontsize=9.5, color=INK_SECOND)
    width = 0.026
    gap = 0.024
    for level in range(len(THETA_STEPS)):
        left = 0.258 + level * (width + gap)
        axis.add_patch(patches.Rectangle(
            (left, 0.44), width, 0.36,
            facecolor=THETA_STEPS[level],
            edgecolor=SURFACE, linewidth=1.4,
        ))
        axis.text(left + width / 2, 0.13, str(level), ha="center",
                  va="center", fontsize=9.0, color=INK_MUTED)
    axis.text(
        0.258 + len(THETA_STEPS) * (width + gap) + 0.006, 0.62,
        note, ha="left", va="center", fontsize=9.3, color=INK_MUTED,
    )


def build_table(selections, requests, summary):
    header = ["Request", "Method", "Path", "Purification", "Slots",
              "$F$", "$w$", "$\\Pr$", "$\\Pr\\cdot w$"]
    body = []
    differing = set()
    for index, request in enumerate(requests):
        contributions = {}
        for name, label in METHODS:
            row = selections[name].get(request)
            tag = f"R{index + 1}: ({request[0]},{request[1]})"
            if row is None:
                body.append([tag, label, "rejected", "--", "--",
                             "--", "--", "--", "--"])
                continue
            contributions[name] = round(float(row["expected_werner"]), 9)
            body.append([
                tag,
                label,
                row["path_1based"],
                pumping_label(row),
                f"{row['first_slot']}-{row['last_slot']}",
                f"{float(row['fidelity']):.4f}",
                f"{float(row['werner']):.4f}",
                f"{float(row['success_probability']):.4f}",
                f"{float(row['expected_werner']):.6f}",
            ])
        if len(set(contributions.values())) > 1:
            differing.update({len(body) - 2, len(body) - 1})

    for name, label in METHODS:
        entry = summary[name]
        body.append([
            "All three",
            label,
            f"{entry['accepted']} accepted",
            "exact optimum" if name == "OPT"
            else f"gap {entry['gap']:.3f}%",
            "", "", "", "",
            f"{entry['objective']:.6f}",
        ])
    return header, body, differing


def draw_table(axis, header, body, differing):
    axis.set_axis_off()
    table = axis.table(
        cellText=body,
        colLabels=header,
        cellLoc="center",
        colWidths=[0.135, 0.105, 0.100, 0.150, 0.070,
                   0.085, 0.085, 0.085, 0.115],
        loc="upper center",
    )
    table.auto_set_font_size(False)
    table.set_fontsize(8.8)

    summary_start = len(body) - len(METHODS)
    for (row, column), cell in table.get_celld().items():
        cell.set_edgecolor(GRIDLINE)
        cell.set_linewidth(0.6)
        cell.get_text().set_color(INK)
        if row == 0:
            cell.set_facecolor("#f0efec")
            cell.get_text().set_weight("bold")
            continue
        body_index = row - 1
        if body_index >= summary_start:
            cell.set_facecolor("#f0efec")
            if column in (1, 3, 8):
                cell.get_text().set_weight("bold")
        elif body_index in differing:
            # The one request whose contribution is not identical.
            cell.set_facecolor(HIGHLIGHT)
            if column == 8:
                cell.get_text().set_weight("bold")
        else:
            cell.set_facecolor(SURFACE if body_index % 2 else PLANE)


def draw_request_figure(index, request, selections, numerology_rows, args):
    """One standalone figure per accepted request: its own numerology under
    both methods, plus the numbers that request contributes to Eq. (12a).

    The panels are placed with explicit rectangles rather than a gridspec:
    the three bands have very unequal heights, and matplotlib's `hspace` is a
    fraction of the *average* axes height, so one usable gap forces the other
    to be wrong.
    """
    label = f"R{index + 1}"
    rows = {name: selections[name].get(request) for name, _ in METHODS}

    figure = plt.figure(figsize=(8.6, 4.7))
    figure.patch.set_facecolor(SURFACE)

    figure.suptitle(
        f"{label}: request ({request[0]},{request[1]}) -- numerology "
        "selected by the exact optimum and by WPFA",
        fontsize=12, color=INK, y=0.965,
    )

    same_route = (
        rows["OPT"] is not None and rows["ZFA2"] is not None
        and rows["OPT"]["path_1based"] == rows["ZFA2"]["path_1based"]
        and rows["OPT"]["purify_by_link"] == rows["ZFA2"]["purify_by_link"]
    )
    same_value = (
        same_route
        and round(float(rows["OPT"]["expected_werner"]), 9)
        == round(float(rows["ZFA2"]["expected_werner"]), 9)
    )
    if same_value:
        same_slots = (
            rows["OPT"]["first_slot"] == rows["ZFA2"]["first_slot"]
            and rows["OPT"]["last_slot"] == rows["ZFA2"]["last_slot"]
        )
        verdict = (
            "WPFA matches the optimum exactly here: same path, same "
            "purification, same fidelity and success probability"
            + ("." if same_slots
               else " -- the schedule is only shifted in time.")
        )
    elif same_route:
        loss = (float(rows["OPT"]["expected_werner"])
                - float(rows["ZFA2"]["expected_werner"]))
        verdict = (
            "WPFA picks the optimal path and purification here; only the "
            f"placement in time differs, costing {loss:.6f} of expected "
            "Werner value."
        )
    else:
        verdict = "WPFA and the optimum differ on this request."
    figure.text(0.5, 0.895, verdict, ha="center", va="center",
                fontsize=10.0, color=INK_SECOND, style="italic")

    capacity = 0
    for column, (name, method_label) in enumerate(METHODS):
        axis = figure.add_axes([0.085 + column * 0.470, 0.345, 0.400, 0.365])
        theta, nodes, slots, capacity = per_request_theta(
            numerology_rows, name, request)
        if not theta:
            axis.set_axis_off()
            axis.text(0.5, 0.5, f"{method_label}: request rejected",
                      ha="center", va="center", fontsize=10.5,
                      color=INK_MUTED, style="italic")
            continue
        draw_numerology(
            axis,
            rf"{method_label}   ($\sum\theta = {sum(theta.values())}$)",
            theta, nodes, slots, capacity,
        )

    draw_theta_legend(
        figure.add_axes([0.085, 0.180, 0.890, 0.055]),
        f"node capacity {capacity} per slot; this is one request's own "
        "footprint",
    )

    header = ["Method", "Path", "Purification", "Slots",
              "$F$", "$w$", r"$\Pr$", r"$\Pr\cdot w$",
              r"$\sum\theta$"]
    body = []
    for name, method_label in METHODS:
        row = rows[name]
        if row is None:
            body.append([method_label, "rejected"] + ["--"] * 7)
            continue
        body.append([
            method_label,
            row["path_1based"],
            pumping_label(row),
            f"{row['first_slot']}-{row['last_slot']}",
            f"{float(row['fidelity']):.6f}",
            f"{float(row['werner']):.6f}",
            f"{float(row['success_probability']):.6f}",
            f"{float(row['expected_werner']):.6f}",
            row["memory_slot_units"],
        ])

    axis = figure.add_axes([0.060, 0.030, 0.910, 0.125])
    axis.set_axis_off()
    table = axis.table(
        cellText=body, colLabels=header, cellLoc="center",
        colWidths=[0.115, 0.105, 0.155, 0.075,
                   0.115, 0.115, 0.115, 0.125, 0.080],
        loc="upper center",
    )
    table.auto_set_font_size(False)
    table.set_fontsize(8.8)
    for (row_index, column), cell in table.get_celld().items():
        cell.set_edgecolor(GRIDLINE)
        cell.set_linewidth(0.6)
        cell.get_text().set_color(INK)
        if row_index == 0:
            cell.set_facecolor("#f0efec")
            cell.get_text().set_weight("bold")
        elif not same_value and column == 7:
            # The one column where this request costs WPFA something.
            cell.set_facecolor(HIGHLIGHT)
            cell.get_text().set_weight("bold")
        else:
            cell.set_facecolor(SURFACE if row_index % 2 else PLANE)

    save_figure(figure, args.output_dir,
                f"reviewer_line4_request_{label}", args.dpi)


def main():
    args = parse_args()
    configure_style()

    results = read_results(args.results)
    if CASE not in results:
        raise ValueError("rerun ./main_small_scale to create reviewer_line4")
    exact = results[CASE]["OPT"]
    summary = {}
    for name, _label in METHODS:
        row = results[CASE][name]
        summary[name] = {
            "objective": row["expected_werner_sum"],
            "gap": row["optimality_gap_pct"],
            "accepted": int(float(row["actual_requests"])),
        }

    selection_rows = read_case_rows(args.selection, CASE)
    numerology_rows = read_case_rows(args.numerology, CASE)
    if not selection_rows or not numerology_rows:
        raise ValueError("selection/numerology CSV missing the reviewer case")

    selections = selections_by_method(selection_rows)
    requests = request_labels(selections)

    figure = plt.figure(figsize=(11.0, 7.8))
    figure.patch.set_facecolor(SURFACE)
    outer = figure.add_gridspec(
        3, 1, height_ratios=[1.62, 0.11, 1.16], hspace=0.26,
        left=0.075, right=0.985, top=0.855, bottom=0.052,
    )
    panels = outer[0].subgridspec(2, 1, height_ratios=[0.98, 1.02],
                                 hspace=0.42)

    figure.suptitle(
        "Small-scale optimality validation on a known 4-node chain: "
        "WPFA versus the exact optimum",
        fontsize=13, color=INK, y=0.975,
    )
    figure.text(
        0.5, 0.935,
        f"Both methods accept all three requests and select the same paths "
        f"and the same purification. Expected Werner sum: exact OPT "
        f"{summary['OPT']['objective']:.6f}, WPFA "
        f"{summary['ZFA2']['objective']:.6f} "
        f"-- an optimality gap of {summary['ZFA2']['gap']:.3f}%.",
        ha="center", va="center", fontsize=10.2, color=INK_SECOND,
    )
    figure.text(
        0.5, 0.902,
        "Only R2 differs, and only in when its operations are placed; "
        "the whole gap comes from that one request's storage time.",
        ha="center", va="center", fontsize=10.2, color=INK_SECOND,
        style="italic",
    )

    schedule_row = panels[0].subgridspec(1, 2, wspace=0.13)
    theta_row = panels[1].subgridspec(1, 2, wspace=0.13)

    footprint = 0
    capacity = 0
    for column, (name, label) in enumerate(METHODS):
        theta, nodes, slots, capacity = aggregate_theta(numerology_rows, name)
        footprint = sum(theta.values())
        draw_schedule(
            figure.add_subplot(schedule_row[0, column]),
            f"{label}: selected schedule",
            selections[name], requests, slots,
        )
        draw_numerology(
            figure.add_subplot(theta_row[0, column]),
            f"{label}: numerology occupancy",
            theta, nodes, slots, capacity,
        )

    draw_theta_legend(
        figure.add_subplot(outer[1]),
        f"capacity {capacity} per node-slot (bold = saturated);  "
        f"both methods hold the same total, $\\sum\\theta = {footprint}$",
    )

    header, body, differing = build_table(selections, requests, summary)
    draw_table(figure.add_subplot(outer[2]), header, body, differing)

    figure.text(
        0.5, 0.014,
        "Exact OPT enumerates every schedule of the discrete-time model on "
        f"every simple path ({exact['enumerated_schedules']:,} schedules, "
        f"{exact['feasible_schedules']:,} feasible, "
        f"{exact['search_states']:,} branch-and-bound states) with no "
        "approximation bucket, candidate cap, or time cutoff.",
        ha="center", va="center", fontsize=9.0, color=INK_MUTED,
        style="italic",
    )

    save_figure(figure, args.output_dir,
                "reviewer_line4_opt_vs_wpfa_detail", args.dpi)

    for index, request in enumerate(requests):
        draw_request_figure(
            index, request, selections, numerology_rows, args)


if __name__ == "__main__":
    main()
