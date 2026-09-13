"""Waxman, Grid, and RGG topology generator for the experiment suite."""
import csv
import hashlib
import os
import sys
import networkx as nx
import random
import numpy
import math
import tempfile
import time
from contextlib import contextmanager
from math import ceil
from pathlib import Path

RANGE = 300


@contextmanager
def exclusive_file_lock(filename):
    """Serialize updates from the experiment's parallel generator processes."""
    with open(filename, "a+b") as lock_file:
        lock_file.seek(0, os.SEEK_END)
        if lock_file.tell() == 0:
            lock_file.write(b"0")
            lock_file.flush()
        lock_file.seek(0)
        if os.name == "nt":
            import msvcrt
            while True:
                try:
                    msvcrt.locking(lock_file.fileno(), msvcrt.LK_NBLCK, 1)
                    break
                except OSError:
                    time.sleep(0.05)
        else:
            import fcntl
            fcntl.flock(lock_file.fileno(), fcntl.LOCK_EX)
        try:
            yield
        finally:
            lock_file.seek(0)
            if os.name == "nt":
                msvcrt.locking(lock_file.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(lock_file.fileno(), fcntl.LOCK_UN)


def record_topology_distance(
        output_filename, topology, node_count, edge_count, average_km, seed):
    """Upsert this generated instance into the shared topology-distance CSV."""
    input_path = Path(output_filename).resolve()
    if input_path.parent.name.lower() == "input":
        data_directory = input_path.parent.parent
        answer_directory = data_directory / "ans"
        displayed_input = input_path.relative_to(data_directory).as_posix()
    else:
        answer_directory = input_path.parent
        displayed_input = input_path.name
    answer_directory.mkdir(parents=True, exist_ok=True)

    summary_path = answer_directory / "topology_average_distance.csv"
    lock_key = hashlib.sha256(
        str(summary_path).encode("utf-8")).hexdigest()[:20]
    lock_path = Path(tempfile.gettempdir()) / (
        "topology_average_distance_" + lock_key + ".lock")
    fieldnames = [
        "topology", "input_file", "seed", "nodes", "edges",
        "topology_scale_km", "average_link_distance_km",
    ]
    new_row = {
        "topology": topology,
        "input_file": displayed_input,
        "seed": "" if seed is None else seed,
        "nodes": node_count,
        "edges": edge_count,
        "topology_scale_km": RANGE,
        "average_link_distance_km": format(average_km, ".17g"),
    }

    with exclusive_file_lock(lock_path):
        rows = {}
        if summary_path.exists():
            with summary_path.open(newline="", encoding="utf-8") as summary:
                for row in csv.DictReader(summary):
                    if row.get("input_file"):
                        rows[row["input_file"]] = row
        rows[displayed_input] = new_row

        temporary_path = summary_path.with_suffix(".csv.tmp")
        with temporary_path.open("w", newline="", encoding="utf-8") as output:
            writer = csv.DictWriter(output, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(
                rows[key]
                for key in sorted(
                    rows,
                    key=lambda item: (rows[item].get("topology", ""), item))
            )
        os.replace(temporary_path, summary_path)

def dist(p1, p2):
    (x1, y1) = p1
    (x2, y2) = p2
    return ((x1 - x2) ** 2 + (y1 - y2) ** 2) ** (1 / 2)

def link_prob(entangle_lambda, dis, times):
    one_prob = math.exp(-entangle_lambda * dis)
    print("one_prob =", 1 - ((1 - one_prob) ** times), file=sys.stderr)
    return 1 - ((1 - one_prob) ** times)

def generate_waxman_topology(node_count, seed):
    generation_attempt = 0
    while True:
        graph_seed = (seed + generation_attempt if seed is not None else None)
        graph = nx.waxman_graph(
            node_count, beta=0.85, alpha=0.08,
            domain=(0, 0, 0.5, 1), seed=graph_seed)
        generation_attempt += 1

        positions = nx.get_node_attributes(graph, 'pos')
        add_edge = []
        # Preserve the original Waxman post-processing exactly: connect each
        # node to its nearest higher-index node when that edge is absent.
        for u in range(graph.order() - 1):
            mi_dist = dist(positions[u], positions[graph.order() - 1])
            mi_idx = graph.order() - 1
            for v in range(u + 1, graph.order()):
                if graph.has_edge(u, v):
                    continue
                if mi_dist > dist(positions[u], positions[v]):
                    mi_dist = dist(positions[u], positions[v])
                    mi_idx = v
            if not graph.has_edge(u, mi_idx):
                add_edge.append((u, mi_idx))

        graph.add_edges_from(add_edge)
        if nx.is_connected(graph):
            return graph, generation_attempt, None
        print("topo is not connected", file=sys.stderr)

def generate_grid_topology(node_count):
    # Use a near-square, standard 4-neighbour rectangular grid.  For a node
    # count that is not a perfect square, keep a connected row-major prefix.
    rows = max(1, int(math.floor(math.sqrt(node_count))))
    columns = int(math.ceil(node_count / rows))
    coordinate_graph = nx.grid_2d_graph(rows, columns)
    selected_coordinates = list(coordinate_graph.nodes())[:node_count]
    coordinate_graph = coordinate_graph.subgraph(selected_coordinates).copy()
    node_id = {
        coordinate: index
        for index, coordinate in enumerate(selected_coordinates)
    }
    graph = nx.relabel_nodes(coordinate_graph, node_id, copy=True)

    row_denominator = max(1, rows - 1)
    column_denominator = max(1, columns - 1)
    positions = {
        node_id[(row, column)]: (
            0.5 * row / row_denominator,
            column / column_denominator)
        for row, column in selected_coordinates
    }
    nx.set_node_attributes(graph, positions, 'pos')
    return graph, 1, None

def generate_rgg_topology(node_count, seed):
    # Radius 0.17 gives a connected sparse graph at n=100 with density close
    # enough to the Waxman instances for a meaningful topology comparison.
    # Retry with deterministic seeds; very rare prolonged failures gradually
    # increase the radius so graph generation cannot loop forever.
    generation_attempt = 0
    radius = 0.17
    while True:
        graph_seed = (seed + generation_attempt if seed is not None else None)
        graph = nx.random_geometric_graph(
            node_count, radius=radius, seed=graph_seed)
        generation_attempt += 1
        if nx.is_connected(graph):
            positions = nx.get_node_attributes(graph, 'pos')
            positions = {
                node: (0.5 * position[0], position[1])
                for node, position in positions.items()
            }
            nx.set_node_attributes(graph, positions, 'pos')
            return graph, generation_attempt, radius
        if generation_attempt % 100 == 0:
            radius += 0.01

def normalize_topology_model(value):
    aliases = {
        "0": "waxman", "waxman": "waxman",
        "1": "grid", "grid": "grid",
        "2": "rgg", "rgg": "rgg",
    }
    normalized = aliases.get(value.strip().lower())
    if normalized is None:
        raise ValueError(
            "topology model must be one of: waxman, grid, rgg (or 0, 1, 2)")
    return normalized

def normalize_memory_distribution(value):
    aliases = {
        "0": "uniform", "uniform": "uniform",
        "1": "degree_proportional",
        "degree-proportional": "degree_proportional",
        "degree_proportional": "degree_proportional",
        "2": "inverse_degree",
        "inverse-degree": "inverse_degree",
        "inverse_degree": "inverse_degree",
        "3": "heterogeneous", "heterogeneous": "heterogeneous",
        # Backward-compatible name for the former topology-independent mode.
        "independent": "heterogeneous",
    }
    normalized = aliases.get(value.strip().lower())
    if normalized is None:
        raise ValueError(
            "memory distribution must be one of: uniform, "
            "degree-proportional, inverse-degree, heterogeneous "
            "(or 0, 1, 2, 3)")
    return normalized

def allocate_integer_budget(node_ids, weights, total_memory, minimum=1):
    """Allocate an exact integer budget using the largest-remainder rule."""
    node_count = len(node_ids)
    reserved = minimum * node_count
    if total_memory < reserved:
        raise ValueError(
            f"total memory {total_memory} is below the minimum {reserved}")

    weight_sum = sum(weights)
    if weight_sum <= 0:
        raise ValueError("memory-allocation weights must have a positive sum")

    distributable = total_memory - reserved
    exact_shares = [distributable * weight / weight_sum for weight in weights]
    allocations = [minimum + int(math.floor(share)) for share in exact_shares]
    remainder = total_memory - sum(allocations)
    remainder_order = sorted(
        range(node_count),
        key=lambda index: (-(exact_shares[index] % 1.0), node_ids[index]))
    for index in remainder_order[:remainder]:
        allocations[index] += 1
    return allocations

def generate_memory_offsets(
        graph, distribution, avg_memory, mem_vary, memory_rng):
    node_ids = list(graph.nodes())

    # No average was supplied: retain the historical heterogeneous generator
    # exactly.  All experiments except mem_distribution use this branch.
    if avg_memory is None:
        if distribution != "heterogeneous":
            raise ValueError(
                "avg_memory is required for uniform or degree-based "
                "memory distributions")
        offsets = [memory_rng.randint(-mem_vary, mem_vary) for _ in node_ids]
        return offsets, None

    total_memory = avg_memory * len(node_ids)
    if distribution == "uniform":
        allocations = [avg_memory for _ in node_ids]
    elif distribution == "heterogeneous":
        # Draw unequal capacities independently of topology, then rebalance
        # within the same bounds so every policy has the same total budget.
        lower = max(1, avg_memory - mem_vary)
        upper = max(lower, avg_memory + mem_vary)
        allocations = [
            memory_rng.randint(lower, upper) for _ in node_ids
        ]
        difference = total_memory - sum(allocations)
        adjustment_order = list(range(len(node_ids)))
        while difference != 0:
            memory_rng.shuffle(adjustment_order)
            changed = False
            for index in adjustment_order:
                if difference > 0 and allocations[index] < upper:
                    allocations[index] += 1
                    difference -= 1
                    changed = True
                elif difference < 0 and allocations[index] > lower:
                    allocations[index] -= 1
                    difference += 1
                    changed = True
                if difference == 0:
                    break
            if not changed:
                raise RuntimeError("cannot rebalance heterogeneous memory budget")
    else:
        degrees = [max(1, graph.degree(node)) for node in node_ids]
        if distribution == "degree_proportional":
            weights = [float(degree) for degree in degrees]
        else:
            # High-degree transit nodes receive the least memory.  This is the
            # reviewer-requested bottleneck scenario.
            weights = [1.0 / degree for degree in degrees]
        allocations = allocate_integer_budget(
            node_ids, weights, total_memory, minimum=1)

    offsets = [allocation - avg_memory for allocation in allocations]
    return offsets, allocations

def pearson_correlation(left, right):
    left_mean = sum(left) / len(left)
    right_mean = sum(right) / len(right)
    numerator = sum(
        (x - left_mean) * (y - right_mean)
        for x, y in zip(left, right))
    left_norm = math.sqrt(sum((x - left_mean) ** 2 for x in left))
    right_norm = math.sqrt(sum((y - right_mean) ** 2 for y in right))
    denominator = left_norm * right_norm
    return numerator / denominator if denominator > 0 else 0.0


if len(sys.argv) <= 2:
    print(
        "usage: graph_generator.py OUTPUT NUM_NODES [SEED] [MEM_VARY] "
        "[waxman|grid|rgg] "
        "[uniform|degree-proportional|inverse-degree|heterogeneous] "
        "[AVG_MEMORY]",
        file=sys.stderr)
    sys.exit()

filename = sys.argv[1]
num_of_node = int(sys.argv[2])
experiment_seed = int(sys.argv[3]) if len(sys.argv) >= 4 else None
mem_vary = int(sys.argv[4]) if len(sys.argv) >= 5 else 1
try:
    topology_model = normalize_topology_model(
        sys.argv[5] if len(sys.argv) >= 6 else "waxman")
except ValueError as error:
    print(error, file=sys.stderr)
    sys.exit(2)
try:
    memory_distribution = normalize_memory_distribution(
        sys.argv[6] if len(sys.argv) >= 7 else "heterogeneous")
except ValueError as error:
    print(error, file=sys.stderr)
    sys.exit(2)
avg_memory = int(sys.argv[7]) if len(sys.argv) >= 8 else None
if mem_vary < 0:
    print("mem_vary must be non-negative", file=sys.stderr)
    sys.exit(2)
if avg_memory is not None and avg_memory < 1:
    print("avg_memory must be positive", file=sys.stderr)
    sys.exit(2)
if memory_distribution != "heterogeneous" and avg_memory is None:
    print(
        "avg_memory is required for uniform or degree-based memory "
        "distributions",
        file=sys.stderr)
    sys.exit(2)
if experiment_seed is not None:
    random.seed(experiment_seed)
    numpy.random.seed(experiment_seed % (2 ** 32))
# min_memory_cnt = int(sys.argv[3])
# max_memory_cnt = int(sys.argv[4])
# min_fidelity = float(sys.argv[5])
# max_fidelity = float(sys.argv[6])
# entangle_lambda = float(sys.argv[3])
# tao = float(sys.argv[4])
# entangle_time = float(sys.argv[5])
# entangle_prob = float(sys.argv[3])

print("======== generating graph ========", file=sys.stderr)
print("filename =", filename, file=sys.stderr)
print("num_of_node =", num_of_node, file=sys.stderr)
print("seed =", experiment_seed, file=sys.stderr)
if memory_distribution == "heterogeneous":
    print(
        "memory_offset_range =", f"[-{mem_vary}, +{mem_vary}]",
        file=sys.stderr)
elif memory_distribution == "uniform":
    print("memory_offset_range = [0, 0]", file=sys.stderr)
else:
    print("memory_offset_range = degree-derived", file=sys.stderr)
print("topology_model =", topology_model, file=sys.stderr)
print("memory_distribution =", memory_distribution, file=sys.stderr)
# print("min_fidelity =", min_fidelity, ", max_fidelity =", max_fidelity, file=sys.stderr)
# print("min_memory_cnt =", min_memory_cnt, ", max_memory_cnt =", max_memory_cnt, file=sys.stderr)

if topology_model == "waxman":
    G, generation_attempt, topology_radius = generate_waxman_topology(
        num_of_node, experiment_seed)
elif topology_model == "grid":
    G, generation_attempt, topology_radius = generate_grid_topology(
        num_of_node)
else:
    G, generation_attempt, topology_radius = generate_rgg_topology(
        num_of_node, experiment_seed)

print("topology_generation_attempts =", generation_attempt, file=sys.stderr)
if topology_radius is not None:
    print("topology_radius =", topology_radius, file=sys.stderr)

path = filename

# Keep topology and link lengths identical across the memory-distribution
# sweeps.  Memory allocation uses its own RNG and therefore cannot perturb the
# topology coordinates from which physical link lengths are calculated.
distribution_seed = (experiment_seed if experiment_seed is not None
                     else random.SystemRandom().randrange(2 ** 63))
memory_rng = random.Random(distribution_seed)

memory_offsets, allocated_memories = generate_memory_offsets(
    G, memory_distribution, avg_memory, mem_vary, memory_rng)

with open(path, 'w') as f:
    positions = nx.get_node_attributes(G, 'pos')
    # write node
    print(num_of_node, file=f)
    for n, num_of_memory in zip(G.nodes(), memory_offsets):
        (x, y) = positions[n]
        pos_x = str(x*RANGE)
        pos_y = str(y*RANGE)
        print(num_of_memory, file = f)
    
    # write edge
    num_of_edge = 0
    for e in G.edges():
        if e[0] != e[1]:
            num_of_edge += 1
    print(num_of_edge, file=f)
    avg_l = 0
    for e in G.edges():
        if e[0] != e[1]:
            e0 = str(e[0])
            e1 = str(e[1])
            dis = RANGE * dist(positions[e[0]], positions[e[1]])  # distance
            # The explicit unit marker distinguishes physical lengths from
            # legacy three-column files containing a random fidelity ratio.
            # Graph applies F_e = 1/4 + 3/4 exp(-Gamma * l_e).
            print(e0 + " " + e1 + " " + str(dis) + " km", file=f)
            avg_l += dis
    avg_l /= num_of_edge

print("num_of_edge =", num_of_edge, file=sys.stderr)
print("avg_edge_len =", avg_l, file=sys.stderr)
record_topology_distance(
    filename, topology_model, num_of_node, num_of_edge, avg_l,
    experiment_seed)
print(
    "distance_summary =",
    str((Path(filename).resolve().parent.parent / "ans" /
         "topology_average_distance.csv")
        if Path(filename).resolve().parent.name.lower() == "input"
        else Path(filename).resolve().parent / "topology_average_distance.csv"),
    file=sys.stderr)
print("memory_offset_min =", min(memory_offsets), file=sys.stderr)
print("memory_offset_max =", max(memory_offsets), file=sys.stderr)
print("memory_offset_mean =", sum(memory_offsets) / len(memory_offsets), file=sys.stderr)
if allocated_memories is not None:
    degrees = [G.degree(node) for node in G.nodes()]
    print("memory_total =", sum(allocated_memories), file=sys.stderr)
    print("memory_min =", min(allocated_memories), file=sys.stderr)
    print("memory_max =", max(allocated_memories), file=sys.stderr)
    print(
        "degree_memory_correlation =",
        pearson_correlation(degrees, allocated_memories),
        file=sys.stderr)
print("\n======== graph generate finished ! ========", file=sys.stderr)


# print(prob(entangle_lambda, 150))
