#!/usr/bin/env python3
"""Turn the small-scale selection CSV into response-letter tables.

Reads what main_small_scale wrote and emits, for one case, a per-request
comparison of the exact optimum and WPFA: chosen path, pumping rounds,
end-to-end fidelity, Werner parameter, success probability, the resulting
contribution to Eq. (12a), and the node-time memory footprint of the
numerology.  Both a LaTeX tabular and a Markdown table are produced.
"""

import argparse
import csv
from pathlib import Path

from plot_small_scale_reviewer import locate_data_directory

# Simulator name -> label used in the paper.
ALGORITHM_LABELS = {
    "OPT": "Exact OPT",
    "ZFA2": "WPFA",
    "ZFA": "FNPR",
    "MyAlgo1": "FLTO",
    "MyAlgo3": "FLTO-long",
}


def parse_args():
    data = locate_data_directory()
    parser = argparse.ArgumentParser(
        description="Build the R1.3 optimality-validation tables."
    )
    parser.add_argument(
        "--selection",
        type=Path,
        default=data / "ans" / "main_small_scale_selection.csv",
    )
    parser.add_argument(
        "--results",
        type=Path,
        default=data / "ans" / "main_small_scale_results.csv",
    )
    parser.add_argument(
        "--numerology",
        type=Path,
        default=data / "ans" / "main_small_scale_numerology.csv",
    )
    parser.add_argument(
        "--instance",
        type=Path,
        default=data / "ans" / "main_small_scale_instance.csv",
    )
    parser.add_argument("--output-dir", type=Path, default=data / "ans")
    parser.add_argument("--case", default="reviewer_line4")
    parser.add_argument(
        "--algorithms",
        nargs="+",
        default=["OPT", "ZFA2"],
        help="simulator algorithm names, in table order",
    )
    return parser.parse_args()


def read_rows(filename, case):
    with filename.open(newline="", encoding="utf-8") as handle:
        return [row for row in csv.DictReader(handle) if row["case"] == case]


def selections_by_algorithm(rows, algorithms):
    result = {name: [] for name in algorithms}
    for row in rows:
        if row["algorithm"] in result:
            result[row["algorithm"]].append(row)
    for name in algorithms:
        result[name].sort(key=lambda row: (int(row["src_1based"]),
                                           int(row["dst_1based"])))
    return result


def request_order(selections):
    """Every request that at least one algorithm accepted, in paper order."""
    requests = []
    for rows in selections.values():
        for row in rows:
            key = (int(row["src_1based"]), int(row["dst_1based"]))
            if key not in requests:
                requests.append(key)
    return sorted(requests)


def find_row(rows, request):
    for row in rows:
        key = (int(row["src_1based"]), int(row["dst_1based"]))
        if key == request:
            return row
    return None


def objective_summary(results_rows, algorithms):
    summary = {}
    for row in results_rows:
        if row["algorithm"] in algorithms:
            summary[row["algorithm"]] = {
                "objective": float(row["expected_werner_sum"]),
                "gap": float(row["optimality_gap_pct"]),
                "accepted": int(float(row["actual_requests"])),
                "runtime_ms": float(row["runtime_ms"]),
            }
    return summary


def pumping_text(row, latex):
    """'(3,4):1' style rounds, or a dash when nothing is purified."""
    rounds = row["purify_by_link"]
    used = [item for item in rounds.split(" ") if not item.endswith(":0")]
    if not used:
        return "--" if latex else "-"
    if latex:
        return ", ".join(item.replace("(", "$(").replace("):", ")$: ")
                         for item in used)
    return ", ".join(used)


def aggregate_grid(numerology_rows, algorithm, node_count, time_limit):
    grid = [[0] * time_limit for _ in range(node_count)]
    capacity = 0
    for row in numerology_rows:
        if row["algorithm"] != algorithm or row["scope"] != "total":
            continue
        node = int(row["node_1based"]) - 1
        time = int(row["time_slot"])
        grid[node][time] = int(row["memory_units"])
        capacity = int(row["node_capacity"])
    return grid, capacity


def request_grid(numerology_rows, algorithm, request, node_count, time_limit):
    """theta of the single schedule this algorithm chose for `request`."""
    tag = f"({request[0]};{request[1]})"
    grid = [[0] * time_limit for _ in range(node_count)]
    capacity = 0
    found = False
    for row in numerology_rows:
        if row["algorithm"] != algorithm or row["scope"] != "per_request":
            continue
        if row["request"] != tag:
            continue
        found = True
        grid[int(row["node_1based"]) - 1][int(row["time_slot"])] = \
            int(row["memory_units"])
        capacity = int(row["node_capacity"])
    return (grid if found else None), capacity


def grid_dimensions(numerology_rows):
    nodes = max(int(row["node_1based"]) for row in numerology_rows)
    slots = max(int(row["time_slot"]) for row in numerology_rows) + 1
    return nodes, slots


def grid_table(grid, node_count, time_limit):
    """One numerology grid as a Markdown table, latest slot first."""
    header = ["slot"] + [f"v{node + 1}" for node in range(node_count)]
    rows = []
    for time in reversed(range(time_limit)):
        rows.append(
            [f"t{time}"] +
            [str(grid[node][time]) if grid[node][time] else "."
             for node in range(node_count)]
        )
    return markdown_table(header, rows)


def write_latex(path, case, algorithms, selections, requests, summary):
    """The per-request comparison table, column-for-column as in the Markdown
    twin: path, pumping rounds, slot span, F, w, Pr and the Eq. (12a) term.

    Emitted as a one-column `table`; in a two-column layout switch it to
    `table*`, which is why the tabular carries no width assumption.
    """
    lines = [
        "% Generated by src/table_small_scale.py -- do not edit by hand.",
        "% Per-request comparison of the exact optimum and WPFA on the",
        f"% small-scale case '{case}'.",
        "% Two-column layout: change table -> table* .",
        r"\begin{table}[!t]",
        r"\centering",
        r"\footnotesize",
        r"\setlength{\tabcolsep}{4pt}",
        r"\caption{Schedules selected by the exact optimum and by WPFA on the "
        r"four-node chain. Node labels are $1$-based and slots are batch slot "
        r"indices. Pumping lists the purification rounds applied to each "
        r"physical link; $w=(4F-1)/3$; $\Pr$ is the end-to-end success "
        r"probability; $\Pr\cdot w$ is the request's term in the objective "
        r"of Eq.~(12a).}",
        r"\label{tab:small_scale_optimality}",
        r"\begin{tabular}{llccccccc}",
        r"\hline",
        r"Request & Method & Path & Pumping & Slots & $F$ & $w$ & $\Pr$ & "
        r"$\Pr\cdot w$ \\",
        r"\hline",
    ]

    for index, request in enumerate(requests):
        label = f"R{index + 1}: $({request[0]},{request[1]})$"
        for position, algorithm in enumerate(algorithms):
            row = find_row(selections[algorithm], request)
            cell = label if position == 0 else ""
            method = ALGORITHM_LABELS.get(algorithm, algorithm)
            if row is None:
                lines.append(
                    f"{cell} & {method} & rejected & " + " & ".join(["--"] * 6)
                    + r" \\"
                )
                continue
            lines.append(
                f"{cell} & {method} & "
                # A path is a node sequence, so hyphens as in the paper text;
                # the slot span below is a real range, hence an en dash.
                f"{row['path_1based']} & "
                f"{pumping_text(row, True)} & "
                f"{row['first_slot']}--{row['last_slot']} & "
                f"{float(row['fidelity']):.6f} & "
                f"{float(row['werner']):.6f} & "
                f"{float(row['success_probability']):.6f} & "
                f"{float(row['expected_werner']):.6f} \\\\"
            )
        lines.append(r"\hline")

    for algorithm in algorithms:
        entry = summary.get(algorithm)
        if entry is None:
            continue
        method = ALGORITHM_LABELS.get(algorithm, algorithm)
        footprint = sum(
            int(row["memory_slot_units"]) for row in selections[algorithm]
        )
        verdict = ("exact optimum" if algorithm == "OPT"
                   else f"optimality gap {entry['gap']:.3f}\\%")
        lines.append(
            r"\multicolumn{8}{l}{\textbf{" + method + r"}: all "
            f"{entry['accepted']} requests accepted, "
            f"$\\sum\\theta={footprint}$ memory cells, {verdict}"
            r"} & \textbf{"
            f"{entry['objective']:.6f}" + r"} \\"
        )
    lines += [r"\hline", r"\end{tabular}", r"\end{table}", ""]
    path.write_text("\n".join(lines), encoding="utf-8")
    return path


def latex_grid_rows(grid, node_count, time_limit):
    """Numerology grid body: one row per slot, latest slot first."""
    rows = []
    for time in reversed(range(time_limit)):
        cells = [
            str(grid[node][time]) if grid[node][time] else r"$\cdot$"
            for node in range(node_count)
        ]
        rows.append(f"$t_{{{time}}}$ & " + " & ".join(cells) + r" \\")
    return rows


def read_instance(filename, case):
    """Instance parameters as dumped by main_small_scale."""
    instance = {"global": {}, "link": {}, "node": {}, "request": {}}
    with filename.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            if row["case"] != case:
                continue
            value = float(row["value"])
            if row["scope"] == "global":
                instance["global"][row["key"]] = value
            elif row["scope"] == "node":
                node = int(row["node_a"])
                instance["node"].setdefault(node, {})[row["key"]] = value
            else:
                pair = (int(row["node_a"]), int(row["node_b"]))
                instance[row["scope"]].setdefault(
                    pair, {})[row["key"]] = value
    return instance


def write_instance_latex(path, case, instance):
    """The instance disclosure: one table for the fibers and the nodes, one
    for the batch, decoherence and purification settings."""
    values = instance["global"]
    slot_ms = values["slot_duration_s"] * 1e3
    lines = [
        "% Generated by src/table_small_scale.py -- do not edit by hand.",
        f"% Instance parameters of the small-scale case '{case}'.",
        "% Two-column layout: change table -> table* .",
        "",
        r"\begin{table}[!t]",
        r"\centering",
        r"\footnotesize",
        r"\setlength{\tabcolsep}{5pt}",
        r"\caption{Physical parameters of the four-node validation chain. "
        r"$F_e$ and $w_e=(4F_e-1)/3$ are the elementary-pair fidelity and "
        r"Werner parameter of each fiber, $\ell$ its length recovered from "
        r"$w_e=e^{-\Gamma\ell}$, and $\Pr(u,v)$ its entangling success "
        r"probability within one slot. Every node holds the same memory "
        r"capacity and swapping probability.}",
        r"\label{tab:small_scale_instance}",
        r"\begin{tabular}{cccccc}",
        r"\hline",
        r"Fiber $(u,v)$ & ratio & $F_e$ & $w_e$ & $\ell$ (km) & "
        r"$\Pr(u,v)$ \\",
        r"\hline",
    ]
    for pair in sorted(instance["link"]):
        link = instance["link"][pair]
        lines.append(
            f"$({pair[0]},{pair[1]})$ & {link['fidelity_ratio']:.2f} & "
            f"{link['fidelity']:.4f} & {link['werner']:.4f} & "
            f"{link['length_km']:.2f} & "
            f"{link['entangle_probability']:.5f} \\\\"
        )
    lines += [
        r"\hline",
        r"\multicolumn{6}{l}{Every node $v$: $c^t(v) = " +
        f"{int(values['memory_per_node'])}" +
        r"$ memory cells in every slot, swapping success $\Pr(v) = " +
        f"{values['swap_probability']:.2f}" + r"$.} \\",
        r"\multicolumn{6}{l}{Requests: " +
        "; ".join(
            f"R{index + 1} $({source},{destination})$"
            for index, (source, destination)
            in enumerate(sorted(instance["request"]))
        ) +
        r", each with threshold $\widehat{F} = " +
        f"{values['fidelity_threshold']:.2f}" +
        r"$ (equivalently $\widehat{w} = " +
        f"{values['werner_threshold']:.4f}" + r"$).} \\",
        r"\hline",
        r"\end{tabular}",
        r"\end{table}",
        "",
        r"\begin{table}[!t]",
        r"\centering",
        r"\footnotesize",
        r"\setlength{\tabcolsep}{5pt}",
        r"\caption{Batch, decoherence and purification settings of the "
        r"small-scale validation run. All values are those the simulator "
        r"executed with.}",
        r"\label{tab:small_scale_settings}",
        r"\begin{tabular}{llll}",
        r"\hline",
        r"Parameter & Value & Parameter & Value \\",
        r"\hline",
    ]

    left = [
        (r"Batch length $|T|$", f"{int(values['time_limit'])} slots"),
        (r"Slot duration $\delta$", f"{slot_ms:.0f} ms"),
        (r"Batch span $|T|\,\delta$",
         f"{int(values['time_limit']) * slot_ms:.0f} ms"),
        (r"Memory per node $c^t(v)$",
         f"{int(values['memory_per_node'])} cells"),
        (r"Swapping success $\Pr(v)$",
         f"{values['swap_probability']:.2f}"),
        (r"Fidelity threshold $\widehat{F}$",
         f"{values['fidelity_threshold']:.2f}"),
    ]
    right = [
        (r"Coherence time $T_{\mathrm{mem}}$",
         f"{values['memory_coherence_s'] * 1e3:.0f} ms"),
        (r"Decoherence exponent $\kappa$",
         f"{values['decoherence_kappa']:.0f}"),
        (r"Decay per slot $\eta=\delta/T_{\mathrm{mem}}$",
         f"{values['decoherence_eta']:.2f}"),
        (r"Fidelity decay $\Gamma$",
         f"{values['gamma_per_km']:.4f}~km$^{{-1}}$"),
        (r"Fiber loss $\lambda$",
         f"{values['lambda_per_km']:.3f}~km$^{{-1}}$"),
        (r"Attempts per slot $\xi$",
         f"{int(values['entangle_attempts'])}"),
    ]
    extra_left = [
        (r"Attempt duration $\tau_{\mathrm{att}}$",
         f"{values['attempt_duration_s'] * 1e3:.2f} ms"),
    ]
    extra_right = [
        (r"Max pumping rounds",
         f"{int(values['max_purification_rounds'])}"),
    ]
    for (key_a, value_a), (key_b, value_b) in zip(left + extra_left,
                                                  right + extra_right):
        lines.append(f"{key_a} & {value_a} & {key_b} & {value_b} \\\\")

    lines += [r"\hline", r"\end{tabular}", r"\end{table}", ""]
    path.write_text("\n".join(lines), encoding="utf-8")
    return path


def write_numerology_latex(path, case, algorithms, requests, numerology_rows):
    """One table per method: theta_m(t,v) of each accepted request side by
    side with the aggregate the node-time capacity has to absorb."""
    node_count, time_limit = grid_dimensions(numerology_rows)
    node_header = " & ".join(
        f"$v_{node + 1}$" for node in range(node_count))

    lines = [
        "% Generated by src/table_small_scale.py -- do not edit by hand.",
        f"% Per-request numerology grids for the case '{case}'.",
        "% Two-column layout: change table -> table* .",
    ]

    for algorithm in algorithms:
        method = ALGORITHM_LABELS.get(algorithm, algorithm)
        blocks = []
        for index, request in enumerate(requests):
            grid, capacity = request_grid(
                numerology_rows, algorithm, request, node_count, time_limit)
            if grid is None:
                continue
            blocks.append((
                f"R{index + 1}: $({request[0]},{request[1]})$",
                latex_grid_rows(grid, node_count, time_limit),
            ))
        grid, capacity = aggregate_grid(
            numerology_rows, algorithm, node_count, time_limit)
        blocks.append((
            "All accepted",
            latex_grid_rows(grid, node_count, time_limit),
        ))

        # Grids are laid out side by side: slot column, then one node group
        # per block.
        column_spec = "l" + ("|" + "c" * node_count) * len(blocks)
        group_header = " & ".join(
            r"\multicolumn{" + str(node_count) + r"}{c}{" + title + r"}"
            for title, _rows in blocks
        )
        body = []
        for line_index in range(time_limit):
            cells = []
            for _title, rows in blocks:
                cells.append(rows[line_index].split(" & ", 1)[1]
                             .removesuffix(r" \\"))
            slot = rows[line_index].split(" & ", 1)[0]
            body.append(slot + " & " + " & ".join(cells) + r" \\")

        lines += [
            "",
            r"\begin{table}[!t]",
            r"\centering",
            r"\scriptsize",
            r"\setlength{\tabcolsep}{3pt}",
            r"\caption{Numerology $\theta_m(t,v)$ selected by " + method +
            r" on the four-node chain: memory cells held at node $v$ in slot "
            r"$t$, per accepted request and in aggregate. A dot is zero; the "
            r"node-time capacity is " + str(capacity) + r" cells per "
            r"node-slot.}",
            r"\label{tab:numerology_" + algorithm.lower() + r"}",
            r"\begin{tabular}{" + column_spec + r"}",
            r"\hline",
            r"Slot & " + group_header + r" \\",
            r" & " + " & ".join([node_header] * len(blocks)) + r" \\",
            r"\hline",
        ]
        lines += body
        lines += [r"\hline", r"\end{tabular}", r"\end{table}"]

    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")
    return path


def markdown_table(header, rows):
    widths = [len(item) for item in header]
    for row in rows:
        widths = [max(width, len(item)) for width, item in zip(widths, row)]

    def render(items):
        return "| " + " | ".join(
            item.ljust(width) for item, width in zip(items, widths)
        ) + " |"

    lines = [render(header),
             "|" + "|".join("-" * (width + 2) for width in widths) + "|"]
    lines += [render(row) for row in rows]
    return lines


def write_markdown(path, case, algorithms, selections, requests, summary,
                   numerology_rows):
    header = ["Request", "Method", "Path", "Pumping", "Slots", "F", "w",
              "Pr", "Pr*w", "theta"]
    table_rows = []
    for request in requests:
        for algorithm in algorithms:
            row = find_row(selections[algorithm], request)
            method = ALGORITHM_LABELS.get(algorithm, algorithm)
            label = f"({request[0]},{request[1]})"
            if row is None:
                table_rows.append([label, method, "rejected"] + ["-"] * 7)
                continue
            table_rows.append([
                label,
                method,
                row["path_1based"],
                pumping_text(row, False),
                f"{row['first_slot']}..{row['last_slot']}",
                f"{float(row['fidelity']):.6f}",
                f"{float(row['werner']):.6f}",
                f"{float(row['success_probability']):.6f}",
                f"{float(row['expected_werner']):.6f}",
                row["memory_slot_units"],
            ])

    lines = [f"# Small-scale optimality validation ({case})", ""]
    lines += markdown_table(header, table_rows)
    lines += ["", "## Objective", ""]
    summary_rows = []
    for algorithm in algorithms:
        entry = summary.get(algorithm)
        if entry is None:
            continue
        summary_rows.append([
            ALGORITHM_LABELS.get(algorithm, algorithm),
            f"{entry['objective']:.6f}",
            str(entry["accepted"]),
            "exact" if algorithm == "OPT" else f"{entry['gap']:.6f}%",
            f"{entry['runtime_ms']:.3f}",
        ])
    lines += markdown_table(
        ["Method", "Sum Pr*w", "Accepted", "Gap", "Runtime (ms)"],
        summary_rows,
    )

    node_count, time_limit = grid_dimensions(numerology_rows)
    for algorithm in algorithms:
        method = ALGORITHM_LABELS.get(algorithm, algorithm)
        lines += ["", f"## Numerology selected by {method}", ""]

        # One grid per accepted request, then the aggregate they must pack into.
        for index, request in enumerate(requests):
            grid, capacity = request_grid(
                numerology_rows, algorithm, request, node_count, time_limit)
            title = f"### R{index + 1}: request ({request[0]},{request[1]})"
            if grid is None:
                lines += [title + " -- rejected", ""]
                continue
            footprint = sum(sum(row) for row in grid)
            lines += [f"{title}  (sum theta = {footprint})", ""]
            lines += grid_table(grid, node_count, time_limit)
            lines.append("")

        grid, capacity = aggregate_grid(
            numerology_rows, algorithm, node_count, time_limit)
        footprint = sum(sum(row) for row in grid)
        lines += [
            f"### All accepted requests  (sum theta = {footprint}; "
            f"capacity {capacity} per node-slot)",
            "",
        ]
        lines += grid_table(grid, node_count, time_limit)

    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")
    return path


def main():
    args = parse_args()
    selection_rows = read_rows(args.selection, args.case)
    results_rows = read_rows(args.results, args.case)
    numerology_rows = read_rows(args.numerology, args.case)
    if not selection_rows or not results_rows:
        raise ValueError(
            f"no rows for case '{args.case}'; rerun ./main_small_scale"
        )

    selections = selections_by_algorithm(selection_rows, args.algorithms)
    requests = request_order(selections)
    summary = objective_summary(results_rows, args.algorithms)

    args.output_dir.mkdir(parents=True, exist_ok=True)
    latex = write_latex(
        args.output_dir / f"{args.case}_optimality_table.tex",
        args.case, args.algorithms, selections, requests, summary)
    markdown = write_markdown(
        args.output_dir / f"{args.case}_optimality_table.md",
        args.case, args.algorithms, selections, requests, summary,
        numerology_rows)
    instance_latex = None
    if args.instance.exists():
        instance = read_instance(args.instance, args.case)
        if instance["link"]:
            instance_latex = write_instance_latex(
                args.output_dir / f"{args.case}_instance_table.tex",
                args.case, instance)
    numerology_latex = write_numerology_latex(
        args.output_dir / f"{args.case}_numerology_table.tex",
        args.case, args.algorithms, requests, numerology_rows)
    if instance_latex is not None:
        print(f"wrote {instance_latex}")
    print(f"wrote {latex}")
    print(f"wrote {numerology_latex}")
    print(f"wrote {markdown}")


if __name__ == "__main__":
    main()
