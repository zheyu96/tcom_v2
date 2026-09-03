#!/usr/bin/env python3
"""Reviewer-focused topology, optimality, and exact-schedule figures."""

import argparse
import csv
import re
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import patches
from matplotlib.colors import BoundaryNorm, ListedColormap


CASE_ORDER = ["line3", "line4", "diamond4"]
CASE_LABEL = {
    "line3": "3-node line",
    "line4": "4-node line",
    "diamond4": "4-node diamond",
}
NODE_POSITIONS = {
    "line3": {0: (0.08, 0.50), 1: (0.50, 0.50), 2: (0.92, 0.50)},
    "line4": {
        0: (0.04, 0.50), 1: (0.35, 0.50),
        2: (0.65, 0.50), 3: (0.96, 0.50),
    },
    "diamond4": {
        0: (0.08, 0.50), 1: (0.50, 0.84),
        2: (0.50, 0.16), 3: (0.92, 0.50),
    },
}
PURIFY_MEMORY = (
    (1, 1, 0, 0, 0),
    (1, 2, 2, 0, 0),
    (1, 2, 3, 2, 0),
    (1, 2, 3, 3, 2),
)


def locate_data_directory():
    for candidate in (Path("../data"), Path("data")):
        if (candidate / "ans").is_dir() and (candidate / "input").is_dir():
            return candidate
    raise FileNotFoundError("run from the repository root or src directory")


def parse_args():
    data = locate_data_directory()
    parser = argparse.ArgumentParser(
        description="Draw the reviewer-facing small-scale validation figures."
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


def configure_style():
    matplotlib.rcParams.update(
        {
            "font.family": "serif",
            "font.serif": ["Times New Roman", "Times", "DejaVu Serif"],
            # Without this, mathtext falls back to DejaVu Sans and every
            # $...$ label renders sans-serif inside serif body text.  STIX is
            # metric-compatible with Times.
            "mathtext.fontset": "stix",
            "font.size": 10,
            "axes.labelsize": 11,
            "axes.titlesize": 11,
            "xtick.labelsize": 10,
            "ytick.labelsize": 10,
            "pdf.fonttype": 42,
            "ps.fonttype": 42,
            "axes.unicode_minus": False,
        }
    )


def read_results(filename):
    numeric_fields = {
        "nodes": int,
        "edges": int,
        "time_limit": int,
        "memory_per_node": int,
        "requests": int,
        "source": int,
        "destination": int,
        "min_link_fidelity": float,
        "max_link_fidelity": float,
        "fidelity_threshold": float,
        "expected_werner_sum": float,
        "optimality_gap_pct": float,
        "enumerated_schedules": int,
        "feasible_schedules": int,
        "nondominated_candidates": int,
        "search_states": int,
    }
    by_case = {}
    with filename.open(newline="", encoding="utf-8") as source:
        reader = csv.DictReader(source)
        required = {
            "case", "algorithm", "proven_optimal", *numeric_fields.keys()
        }
        missing = required.difference(reader.fieldnames or [])
        if missing:
            raise ValueError(
                "rerun main_small_scale; CSV is missing: "
                + ", ".join(sorted(missing))
            )
        for raw in reader:
            case = raw["case"]
            algorithm = raw["algorithm"]
            row = dict(raw)
            for field, converter in numeric_fields.items():
                if raw[field] != "":
                    row[field] = converter(raw[field])
            row["proven_optimal"] = raw["proven_optimal"] == "1"
            by_case.setdefault(case, {})[algorithm] = row

    for case in CASE_ORDER:
        if case not in by_case or "OPT" not in by_case[case]:
            raise ValueError(f"missing OPT result for {case}")
        if "ZFA2" not in by_case[case]:
            raise ValueError(f"missing ZFA2 result for {case}")
    return by_case


def read_graph(filename, metadata):
    tokens = filename.read_text(encoding="utf-8").split()
    cursor = 0
    node_count = int(tokens[cursor])
    cursor += 1
    memory_offsets = [int(tokens[cursor + index]) for index in range(node_count)]
    cursor += node_count
    edge_count = int(tokens[cursor])
    cursor += 1
    edges = []
    for _ in range(edge_count):
        left = int(tokens[cursor])
        right = int(tokens[cursor + 1])
        ratio = float(tokens[cursor + 2])
        cursor += 3
        fidelity = (
            metadata["min_link_fidelity"]
            + ratio
            * (metadata["max_link_fidelity"] - metadata["min_link_fidelity"])
        )
        edges.append((left, right, fidelity))
    if node_count != metadata["nodes"] or edge_count != metadata["edges"]:
        raise ValueError(f"graph metadata mismatch in {filename}")
    return memory_offsets, edges


def draw_topology(axis, case, metadata, edges, panel_label):
    positions = NODE_POSITIONS[case]
    for edge_index, (left, right, fidelity) in enumerate(edges):
        x_left, y_left = positions[left]
        x_right, y_right = positions[right]
        axis.plot(
            [x_left, x_right], [y_left, y_right],
            color="#454545", linewidth=1.8, zorder=1,
        )
        midpoint_x = (x_left + x_right) / 2
        midpoint_y = (y_left + y_right) / 2
        if case == "diamond4":
            offset_y = 0.055 if midpoint_y >= 0.5 else -0.075
        else:
            offset_y = 0.075 if edge_index % 2 == 0 else -0.095
        axis.text(
            midpoint_x,
            midpoint_y + offset_y,
            rf"$F_e={fidelity:.3f}$",
            ha="center",
            va="center",
            fontsize=8.5,
            bbox={"facecolor": "white", "edgecolor": "none", "pad": 0.8},
            zorder=2,
        )

    source = metadata["source"]
    destination = metadata["destination"]
    request_arrow = patches.FancyArrowPatch(
        positions[source],
        positions[destination],
        connectionstyle="arc3,rad=-0.38",
        arrowstyle="-|>",
        mutation_scale=13,
        linewidth=1.5,
        color="#D62728",
        shrinkA=17,
        shrinkB=17,
        zorder=2,
    )
    axis.add_patch(request_arrow)
    axis.text(
        0.50,
        0.02 if case != "diamond4" else -0.02,
        rf"${metadata['requests']}\times({source}\!\rightarrow\!{destination})$",
        color="#B2181B",
        ha="center",
        va="center",
        fontsize=9,
    )

    for node, (x_value, y_value) in positions.items():
        circle = patches.Circle(
            (x_value, y_value),
            radius=0.075 if metadata["nodes"] == 3 else 0.065,
            facecolor="#4C78A8",
            edgecolor="black",
            linewidth=1.0,
            zorder=4,
        )
        axis.add_patch(circle)
        axis.text(
            x_value, y_value, str(node), color="white",
            ha="center", va="center", weight="bold", zorder=5,
        )

    axis.set_title(f"({panel_label}) {CASE_LABEL[case]}", pad=5)
    axis.text(
        0.5, -0.14,
        rf"$T={metadata['time_limit']}$, "
        rf"$M_v={metadata['memory_per_node']}$, "
        rf"$F_{{th}}={metadata['fidelity_threshold']:.2f}$",
        transform=axis.transAxes,
        ha="center",
        va="top",
        fontsize=9,
    )
    axis.set_xlim(-0.08, 1.08)
    axis.set_ylim(-0.13, 1.06)
    axis.set_aspect("equal")
    axis.axis("off")


def draw_objective_comparison(axis, results):
    x_positions = list(range(len(CASE_ORDER)))
    width = 0.34
    optimum_values = [
        results[case]["OPT"]["expected_werner_sum"] for case in CASE_ORDER
    ]
    zfa2_values = [
        results[case]["ZFA2"]["expected_werner_sum"] for case in CASE_ORDER
    ]
    optimum_bars = axis.bar(
        [position - width / 2 for position in x_positions],
        optimum_values,
        width,
        color="#242424",
        edgecolor="black",
        hatch="///",
        linewidth=0.7,
        label="Exact OPT",
        zorder=3,
    )
    zfa2_bars = axis.bar(
        [position + width / 2 for position in x_positions],
        zfa2_values,
        width,
        color="#D62728",
        edgecolor="black",
        linewidth=0.7,
        label="ZFA2",
        zorder=3,
    )
    for bars, values in (
        (optimum_bars, optimum_values), (zfa2_bars, zfa2_values)
    ):
        for bar, value in zip(bars, values):
            axis.text(
                bar.get_x() + bar.get_width() / 2,
                value + 0.025,
                f"{value:.3f}",
                ha="center",
                va="bottom",
                fontsize=8.5,
            )
    for position, case, value in zip(x_positions, CASE_ORDER, zfa2_values):
        gap = results[case]["ZFA2"]["optimality_gap_pct"]
        axis.text(
            position + width / 2,
            max(0.04, value * 0.52),
            f"gap\n{gap:.2f}%",
            ha="center",
            va="center",
            color="white",
            fontsize=8,
            weight="bold",
        )
    axis.set_xticks(x_positions)
    axis.set_xticklabels([CASE_LABEL[case] for case in CASE_ORDER])
    axis.set_ylabel("Expected Werner-parameter sum")
    axis.set_title("(d) Proposed method versus the exact optimum")
    axis.set_ylim(0, max(optimum_values) * 1.19)
    axis.grid(axis="y", color="#D8D8D8", linewidth=0.7, zorder=0)
    axis.spines["top"].set_visible(False)
    axis.spines["right"].set_visible(False)
    axis.legend(frameon=False, loc="upper center", ncol=2)


def draw_exact_certificate_table(axis, results):
    rows = []
    for case in CASE_ORDER:
        optimum = results[case]["OPT"]
        rows.append(
            [
                CASE_LABEL[case],
                f"{optimum['enumerated_schedules']:,}",
                f"{optimum['feasible_schedules']:,}",
                f"{optimum['search_states']:,}",
                "Yes" if optimum["proven_optimal"] else "No",
            ]
        )
    axis.axis("off")
    axis.set_title("(e) Exhaustive-search certificate", pad=8)
    table = axis.table(
        cellText=rows,
        colLabels=["Case", "Enumerated", "Feasible", "B&B states", "Optimal"],
        cellLoc="center",
        colLoc="center",
        colWidths=[0.28, 0.20, 0.17, 0.20, 0.15],
        loc="center",
    )
    table.auto_set_font_size(False)
    table.set_fontsize(8.5)
    table.scale(1.05, 1.65)
    for (row, _column), cell in table.get_celld().items():
        cell.set_edgecolor("#555555")
        cell.set_linewidth(0.6)
        if row == 0:
            cell.set_facecolor("#D9E3F0")
            cell.set_text_props(weight="bold")
        elif row % 2 == 0:
            cell.set_facecolor("#F2F2F2")
    axis.text(
        0.5,
        0.13,
        "OPT uses no approximation, candidate cap, state cap, or time cutoff.",
        transform=axis.transAxes,
        ha="center",
        va="center",
        fontsize=8.5,
        style="italic",
    )


def read_certificate(filename):
    case_pattern = re.compile(
        r"case=(\w+) proven_optimal=(\d+) objective=([0-9.eE+-]+) "
        r"accepted=(\d+) search_states=(\d+)"
    )
    selection_pattern = re.compile(
        r"selection=(\d+) request=(\d+)->(\d+) path=([0-9-]+) "
        r"purify_rounds=([0-9-]+) node_time_ranges=(.*?) "
        r"fidelity=([0-9.eE+-]+) success_probability=([0-9.eE+-]+) "
        r"expected_werner=([0-9.eE+-]+)"
    )
    cases = {}
    current_case = None
    for raw_line in filename.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        case_match = case_pattern.fullmatch(line)
        if case_match:
            current_case = case_match.group(1)
            cases[current_case] = {
                "proven": case_match.group(2) == "1",
                "objective": float(case_match.group(3)),
                "accepted": int(case_match.group(4)),
                "selections": [],
            }
            continue
        selection_match = selection_pattern.fullmatch(line)
        if selection_match and current_case is not None:
            ranges = []
            for node_text in selection_match.group(6).split(";"):
                node_id, range_text = node_text.split(":", 1)
                node_ranges = []
                for item in range_text.strip("[]").split("|"):
                    start, finish = item.split("-")
                    node_ranges.append((int(start), int(finish)))
                ranges.append((int(node_id), node_ranges))
            cases[current_case]["selections"].append(
                {
                    "path": [int(value) for value in selection_match.group(4).split("-")],
                    "rounds": [int(value) for value in selection_match.group(5).split("-")],
                    "ranges": ranges,
                }
            )
    for case in CASE_ORDER:
        if case not in cases or not cases[case]["proven"]:
            raise ValueError(f"missing proven-optimal certificate for {case}")
    return cases


def memory_occupancy(case_certificate, metadata):
    node_count = metadata["nodes"]
    time_limit = metadata["time_limit"]
    occupancy = [[0 for _ in range(time_limit)] for _ in range(node_count)]
    descriptions = []
    for selection in case_certificate["selections"]:
        range_by_node = dict(selection["ranges"])
        for node, ranges in selection["ranges"]:
            for start, finish in ranges:
                for time in range(start, finish + 1):
                    occupancy[node][time] += 1

        path = selection["path"]
        rounds = selection["rounds"]
        for link, purification_rounds in enumerate(rounds):
            if purification_rounds <= 0:
                continue
            link_start = range_by_node[path[link]][-1][0]
            for offset in range(purification_rounds + 2):
                extra = (
                    PURIFY_MEMORY[purification_rounds]
                    [purification_rounds + 1 - offset]
                    - 1
                )
                if extra <= 0:
                    continue
                time = link_start + offset
                occupancy[path[link]][time] += extra
                occupancy[path[link + 1]][time] += extra
        descriptions.append(
            "-".join(map(str, path))
            + "; r=(" + ",".join(map(str, rounds)) + ")"
        )

    capacity = metadata["memory_per_node"]
    for node_values in occupancy:
        if any(value > capacity for value in node_values):
            raise ValueError("certificate schedule exceeds node-time memory")
    return occupancy, descriptions


def save_figure(figure, output_directory, stem, dpi,
                suffixes=(".pdf", ".png")):
    output_directory.mkdir(parents=True, exist_ok=True)
    for suffix in suffixes:
        output = output_directory / f"{stem}{suffix}"
        options = {"bbox_inches": "tight"}
        if suffix == ".png":
            options["dpi"] = dpi
        figure.savefig(output, **options)
        print(f"saved {output.resolve()}")
    plt.close(figure)


def draw_validation_figure(results, graph_directory, output_directory, dpi):
    figure = plt.figure(figsize=(11.3, 6.6))
    grid = figure.add_gridspec(
        2, 6, height_ratios=[1.0, 1.22], hspace=0.42, wspace=0.58
    )
    panel_labels = ("a", "b", "c")
    for index, case in enumerate(CASE_ORDER):
        axis = figure.add_subplot(grid[0, 2 * index:2 * index + 2])
        metadata = results[case]["OPT"]
        _offsets, edges = read_graph(
            graph_directory / f"small_scale_{case}.input", metadata
        )
        draw_topology(axis, case, metadata, edges, panel_labels[index])

    objective_axis = figure.add_subplot(grid[1, 0:3])
    draw_objective_comparison(objective_axis, results)
    table_axis = figure.add_subplot(grid[1, 3:6])
    draw_exact_certificate_table(table_axis, results)
    save_figure(
        figure, output_directory, "main_small_scale_reviewer_validation", dpi
    )


def draw_schedule_figure(results, certificates, output_directory, dpi):
    figure, axes = plt.subplots(1, 3, figsize=(10.6, 3.25))
    maximum_capacity = max(
        results[case]["OPT"]["memory_per_node"] for case in CASE_ORDER
    )
    color_map = ListedColormap(["#FFFFFF", "#BDD7E7", "#3182BD", "#08519C"])
    norm = BoundaryNorm(
        [value - 0.5 for value in range(maximum_capacity + 2)],
        color_map.N,
    )
    image = None
    for panel_index, (axis, case) in enumerate(zip(axes, CASE_ORDER)):
        metadata = results[case]["OPT"]
        occupancy, descriptions = memory_occupancy(
            certificates[case], metadata
        )
        image = axis.imshow(
            occupancy,
            cmap=color_map,
            norm=norm,
            aspect="auto",
            interpolation="nearest",
        )
        capacity = metadata["memory_per_node"]
        for node, row in enumerate(occupancy):
            for time, value in enumerate(row):
                axis.text(
                    time,
                    node,
                    str(value),
                    ha="center",
                    va="center",
                    color="white" if value >= 2 else "black",
                    fontsize=9,
                    weight="bold" if value == capacity else "normal",
                )
                if value == capacity:
                    axis.add_patch(
                        patches.Rectangle(
                            (time - 0.49, node - 0.49),
                            0.98,
                            0.98,
                            fill=False,
                            edgecolor="#D62728",
                            linewidth=1.3,
                        )
                    )
        axis.set_xticks(range(metadata["time_limit"]))
        axis.set_yticks(range(metadata["nodes"]))
        axis.set_xlabel("Time slot")
        if panel_index == 0:
            axis.set_ylabel("Node")
        axis.set_title(
            f"({chr(ord('a') + panel_index)}) {CASE_LABEL[case]}\n"
            f"OPT={certificates[case]['objective']:.3f}, "
            f"accepted={certificates[case]['accepted']}, "
            f"$M_v$={capacity}",
            fontsize=10.5,
        )
        axis.text(
            0.5,
            -0.27,
            " | ".join(descriptions),
            transform=axis.transAxes,
            ha="center",
            va="top",
            fontsize=8,
        )
    color_bar = figure.colorbar(
        image, ax=axes, ticks=range(maximum_capacity + 1),
        fraction=0.026, pad=0.025,
    )
    color_bar.set_label("Memory cells occupied")
    figure.text(
        0.5,
        0.01,
        "Red outlines mark saturated node–time cells; every value is within capacity.",
        ha="center",
        fontsize=9,
        style="italic",
    )
    figure.subplots_adjust(
        left=0.065, right=0.91, bottom=0.28, top=0.78, wspace=0.32
    )
    save_figure(
        figure, output_directory, "main_small_scale_optimal_schedule", dpi
    )


def main():
    args = parse_args()
    configure_style()
    results = read_results(args.csv)
    certificates = read_certificate(args.certificate)
    draw_validation_figure(
        results, args.graph_dir, args.output_dir, args.dpi
    )
    draw_schedule_figure(
        results, certificates, args.output_dir, args.dpi
    )


if __name__ == "__main__":
    main()
