"""Waxman, Grid, and RGG topology generator for the experiment suite."""
import sys
import networkx as nx
import random
import numpy
import math
from math import ceil

RANGE = 300

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


if len(sys.argv) <= 2:
    print(
        "usage: graph_generator.py OUTPUT NUM_NODES [SEED] [MEM_VARY] "
        "[waxman|grid|rgg]",
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
if mem_vary < 0:
    print("mem_vary must be non-negative", file=sys.stderr)
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
print("memory_offset_range =", f"[-{mem_vary}, +{mem_vary}]", file=sys.stderr)
print("topology_model =", topology_model, file=sys.stderr)
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

# Keep topology and link fidelity identical across the mem_vary sweep.  The
# historical generator drew N values from randint(-1, 1) before drawing link
# fidelities.  Advancing a dedicated fidelity RNG by that same baseline
# sequence preserves all existing mem_vary=1 inputs while allowing the memory
# RNG to use a different range without perturbing any link value.
distribution_seed = (experiment_seed if experiment_seed is not None
                     else random.SystemRandom().randrange(2 ** 63))
memory_rng = random.Random(distribution_seed)
fidelity_rng = random.Random(distribution_seed)
for _ in range(num_of_node):
    fidelity_rng.randint(-1, 1)

with open(path, 'w') as f:
    positions = nx.get_node_attributes(G, 'pos')
    # write node
    print(num_of_node, file=f)
    memory_offsets = []
    for n in G.nodes():
        (x, y) = positions[n]
        pos_x = str(x*RANGE)
        pos_y = str(y*RANGE)
        num_of_memory = memory_rng.randint(-mem_vary, mem_vary)
        memory_offsets.append(num_of_memory)
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
            # F = random.random()*(max_fidelity-min_fidelity) + min_fidelity  # fidelity
            # ratio_list=[0.98,0.7]
            # ratio=numpy.random.choice(ratio_list,p=[0.3,0.7])
            # ratio = numpy.random.normal(0.7, 0.1)
            #dif = abs(1 - ratio)
            #ratio = 1 - dif
            #if ratio > 0.95:
            #    ratio = 0.95
            #if ratio < 0.55:
            #    ratio = 0.55
            # 70% link 在 sweet spot (需 purify), 30% 高 fid (非 purify 也能過)
            # F_init = ratio * 0.15 + 0.80, 最低 F_init >= 0.80 (ratio >= 0)
            ratio = fidelity_rng.uniform(0.75, 0.99)
            if ratio > 0.99:
                ratio = 0.98
            if ratio < 0.75:
                ratio = 0.75
            F = ratio
            print(e0 + " " + e1 + " " + str(F), file=f)
            avg_l += dis
    avg_l /= num_of_edge

print("num_of_edge =", num_of_edge, file=sys.stderr)
print("avg_edge_len =", avg_l, file=sys.stderr)
print("memory_offset_min =", min(memory_offsets), file=sys.stderr)
print("memory_offset_max =", max(memory_offsets), file=sys.stderr)
print("memory_offset_mean =", sum(memory_offsets) / len(memory_offsets), file=sys.stderr)
print("\n======== graph generate finished ! ========", file=sys.stderr)


# print(prob(entangle_lambda, 150))
