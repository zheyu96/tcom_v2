#include "EFiRAP.h"

#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <tuple>

#ifdef EFIRAP_USE_GUROBI
#include "gurobi_c++.h"
#define EFIRAP_HAS_GUROBI 1
#else
#define EFIRAP_HAS_GUROBI 0
#endif

namespace {

constexpr double DIJKSTRA_EPS = 1e-12;
constexpr double MARGINAL_TIE_EPS = 1e-10;

bool has_prefix(const Path& path, const Path& prefix) {
    if(path.size() < prefix.size()) return false;
    for(size_t i = 0; i < prefix.size(); ++i) {
        if(path[i] != prefix[i]) return false;
    }
    return true;
}

} // namespace

EFiRAP::EFiRAP(const Graph& graph,
               const vector<SDpair>& requests,
               const map<SDpair, vector<Path>>& paths,
               int k_paths,
               double mip_gap,
               double solver_time_limit_seconds)
    : AlgorithmBase(graph, requests, paths),
      k_paths(k_paths),
      mip_gap(mip_gap),
      solver_time_limit_seconds(solver_time_limit_seconds) {
    if(k_paths <= 0) {
        throw invalid_argument("EFiRAP requires k_paths > 0");
    }
    if(mip_gap < 0.0 || mip_gap >= 1.0) {
        throw invalid_argument("EFiRAP requires 0 <= mip_gap < 1");
    }
    if(solver_time_limit_seconds < 0.0) {
        throw invalid_argument("EFiRAP requires a non-negative solver time limit");
    }

    algorithm_name = "EFiRAP";
    for(int node = 0; node < this->graph.get_num_nodes(); ++node) {
        for(int t = 0; t < this->graph.get_time_limit(); ++t) {
            initial_memory_capacity += this->graph.get_node_memory_at(node, t);
        }
    }
}

bool EFiRAP::gurobi_available() {
    return EFIRAP_HAS_GUROBI != 0;
}

void EFiRAP::build_request_groups() {
    map<SDpair, int> demand_by_pair;
    for(const SDpair& request : requests) {
        demand_by_pair[request]++;
    }

    request_groups.clear();
    for(const auto& entry : demand_by_pair) {
        RequestGroup group;
        group.sd = entry.first;
        group.demand = entry.second;
        group.yen_paths = yen_k_shortest_paths(
            group.sd.first, group.sd.second, k_paths);
        request_groups.push_back(group);
    }
}

double EFiRAP::path_cost(const Path& path) {
    if(path.size() < 2) return numeric_limits<double>::infinity();

    double result = 0.0;
    for(size_t i = 1; i < path.size(); ++i) {
        double fidelity = graph.get_F_init(path[i - 1], path[i]);
        result -= log(max(fidelity, 1e-15));
    }
    return result;
}

Path EFiRAP::shortest_path(
    int src,
    int dst,
    const set<int>& banned_nodes,
    const set<pair<int, int>>& banned_edges) {
    const int node_count = graph.get_num_nodes();
    if(src < 0 || src >= node_count || dst < 0 || dst >= node_count) return {};
    if(banned_nodes.count(src) || banned_nodes.count(dst)) return {};

    vector<double> distance(node_count, numeric_limits<double>::infinity());
    vector<int> parent(node_count, -1);
    vector<bool> settled(node_count, false);
    using QueueItem = pair<double, int>;
    priority_queue<QueueItem, vector<QueueItem>, greater<QueueItem>> queue;

    distance[src] = 0.0;
    queue.push({0.0, src});

    while(!queue.empty()) {
        double current_distance = queue.top().first;
        int current = queue.top().second;
        queue.pop();

        if(current_distance > distance[current] + DIJKSTRA_EPS) continue;
        if(settled[current]) continue;
        settled[current] = true;
        if(current == dst) break;

        for(int next : graph.adj_list[current]) {
            if(banned_nodes.count(next)) continue;
            if(banned_edges.count({current, next})) continue;
            if(settled[next]) continue;

            double fidelity = graph.get_F_init(current, next);
            double weight = -log(max(fidelity, 1e-15));
            double candidate_distance = current_distance + weight;

            bool strictly_better = candidate_distance + DIJKSTRA_EPS < distance[next];
            bool tie_with_better_parent =
                fabs(candidate_distance - distance[next]) <= DIJKSTRA_EPS &&
                (parent[next] == -1 || current < parent[next]);
            if(strictly_better || tie_with_better_parent) {
                distance[next] = candidate_distance;
                parent[next] = current;
                queue.push({candidate_distance, next});
            }
        }
    }

    if(!isfinite(distance[dst])) return {};

    Path path;
    int steps = 0;
    for(int current = dst; current != -1; current = parent[current]) {
        path.push_back(current);
        if(++steps > node_count) return {};
    }
    reverse(path.begin(), path.end());
    if(path.empty() || path.front() != src) return {};
    return path;
}

vector<Path> EFiRAP::yen_k_shortest_paths(int src, int dst, int K) {
    vector<Path> accepted;
    Path first = shortest_path(src, dst, {}, {});
    if(first.empty()) return accepted;

    accepted.push_back(first);
    set<Path> accepted_set{first};

    using PathCandidate = pair<double, Path>;
    priority_queue<PathCandidate, vector<PathCandidate>, greater<PathCandidate>> pool;
    set<Path> pooled_paths;

    while((int)accepted.size() < K) {
        const Path& previous = accepted.back();
        for(size_t spur_index = 0; spur_index + 1 < previous.size(); ++spur_index) {
            Path root(previous.begin(), previous.begin() + spur_index + 1);
            set<pair<int, int>> banned_edges;
            set<int> banned_nodes;

            for(const Path& chosen : accepted) {
                if(chosen.size() > spur_index + 1 && has_prefix(chosen, root)) {
                    banned_edges.insert({chosen[spur_index], chosen[spur_index + 1]});
                }
            }
            for(size_t i = 0; i + 1 < root.size(); ++i) {
                banned_nodes.insert(root[i]);
            }

            Path spur = shortest_path(root.back(), dst, banned_nodes, banned_edges);
            if(spur.empty()) continue;

            Path total = root;
            total.pop_back();
            total.insert(total.end(), spur.begin(), spur.end());
            if(accepted_set.count(total) || pooled_paths.count(total)) continue;

            pool.push({path_cost(total), total});
            pooled_paths.insert(total);
        }

        if(pool.empty()) break;
        Path next = pool.top().second;
        pool.pop();
        pooled_paths.erase(next);
        accepted.push_back(next);
        accepted_set.insert(next);
    }

    return accepted;
}

int EFiRAP::assign_balanced_swap_times(
    int left,
    int right,
    int start_time,
    vector<int>& swap_time) const {
    if(right == left + 1) return start_time + 1;

    int middle = (left + right) / 2;
    int left_completion =
        assign_balanced_swap_times(left, middle, start_time, swap_time);
    int right_completion =
        assign_balanced_swap_times(middle, right, start_time, swap_time);
    swap_time[middle] = max(left_completion, right_completion);
    return swap_time[middle] + 1;
}

Shape_vector EFiRAP::build_balanced_shape(const Path& path, int start_time) {
    if(path.size() < 2 || start_time < 0) return {};

    vector<int> swap_time(path.size(), -1);
    int completion = assign_balanced_swap_times(
        0, (int)path.size() - 1, start_time, swap_time);
    if(completion >= graph.get_time_limit()) return {};

    Shape_vector shape_vector;
    shape_vector.reserve(path.size());
    for(size_t i = 0; i < path.size(); ++i) {
        if(i == 0 || i + 1 == path.size()) {
            shape_vector.push_back({path[i], {{start_time, completion}}});
        } else {
            shape_vector.push_back({
                path[i],
                {{start_time, swap_time[i]}, {start_time, swap_time[i]}}
            });
        }
    }
    return shape_vector;
}

double EFiRAP::evaluate_fidelity(
    const Shape_vector& shape_vector,
    const vector<int>& purify_rounds) const {
    if(shape_vector.empty()) return 0.0;
    Shape shape(shape_vector, purify_rounds);
    return shape.get_fidelity(
        A, B, n, T, tao, graph.get_F_init(), true);
}

map<EFiRAP::ResourceKey, int> EFiRAP::calculate_memory_usage(
    const Shape_vector& shape_vector,
    const vector<int>& purify_rounds) const {
    map<ResourceKey, int> usage;

    // A primary Bell-pair cell is released when its Shape lifetime ends, so it
    // can be reused by a later connection in the same simulation horizon.
    for(const auto& node_ranges : shape_vector) {
        int node = node_ranges.first;
        for(const auto& range : node_ranges.second) {
            for(int t = range.first; t <= range.second; ++t) {
                usage[{node, t}]++;
            }
        }
    }

    // EFiRAP generates all sacrificial pairs simultaneously. A round therefore
    // adds one cell at both endpoints at the link's generation time. These
    // cells are consumed by purification and released after that timeslot.
    for(size_t link = 0; link + 1 < shape_vector.size(); ++link) {
        int rounds = link < purify_rounds.size() ? purify_rounds[link] : 0;
        if(rounds <= 0) continue;

        int generation_time = shape_vector[link].second.back().first;
        int left_node = shape_vector[link].first;
        int right_node = shape_vector[link + 1].first;
        usage[{left_node, generation_time}] += rounds;
        usage[{right_node, generation_time}] += rounds;
    }
    return usage;
}

bool EFiRAP::fits_initial_memory(const map<ResourceKey, int>& usage) {
    for(const auto& entry : usage) {
        int node = entry.first.first;
        int time = entry.first.second;
        if(time < 0 || time >= graph.get_time_limit()) return false;
        if(graph.get_node_memory_at(node, time) < entry.second) return false;
    }
    return true;
}

bool EFiRAP::fits_current_memory(const map<ResourceKey, int>& usage) {
    return fits_initial_memory(usage);
}

vector<EFiRAP::PurificationScheme> EFiRAP::prepare_path_schemes(
    const Path& path) {
    vector<PurificationScheme> result;
    Shape_vector base_shape = build_balanced_shape(path, 0);
    if(base_shape.empty()) return result;

    const int link_count = (int)path.size() - 1;
    queue<vector<int>> frontier;
    set<vector<int>> visited;
    frontier.push(vector<int>(link_count, 0));

    while(!frontier.empty()) {
        vector<int> rounds = frontier.front();
        frontier.pop();
        if(!visited.insert(rounds).second) continue;

        map<ResourceKey, int> usage = calculate_memory_usage(base_shape, rounds);
        if(!fits_initial_memory(usage)) continue;

        double fidelity = evaluate_fidelity(base_shape, rounds);
        if(fidelity >= graph.get_fidelity_threshold()) {
            result.push_back({rounds, fidelity});
            continue;
        }

        double best_gain = 0.0;
        vector<pair<vector<int>, double>> next_states;
        for(int link = 0; link < link_count; ++link) {
            vector<int> next = rounds;
            next[link]++;
            map<ResourceKey, int> next_usage =
                calculate_memory_usage(base_shape, next);
            if(!fits_initial_memory(next_usage)) continue;

            double next_fidelity = evaluate_fidelity(base_shape, next);
            double gain = next_fidelity - fidelity;
            if(gain > best_gain) best_gain = gain;
            next_states.push_back({next, gain});
        }

        if(best_gain <= MARGINAL_TIE_EPS) continue;
        double tolerance = MARGINAL_TIE_EPS * max(1.0, fabs(best_gain));
        for(const auto& state : next_states) {
            if(state.second + tolerance >= best_gain) {
                frontier.push(state.first);
            }
        }
    }

    sort(result.begin(), result.end(), [](const PurificationScheme& left,
                                          const PurificationScheme& right) {
        int left_pairs = 0;
        int right_pairs = 0;
        for(int rounds : left.rounds) left_pairs += rounds;
        for(int rounds : right.rounds) right_pairs += rounds;
        if(left_pairs != right_pairs) return left_pairs < right_pairs;
        if(left.rounds != right.rounds) return left.rounds < right.rounds;
        return left.fidelity > right.fidelity;
    });
    result.erase(unique(result.begin(), result.end(),
                        [](const PurificationScheme& left,
                           const PurificationScheme& right) {
                            return left.rounds == right.rounds;
                        }),
                 result.end());
    return result;
}

void EFiRAP::prepare_candidates() {
    candidates.clear();
    candidates.resize(request_groups.size());

    for(size_t group_index = 0; group_index < request_groups.size(); ++group_index) {
        for(const Path& path : request_groups[group_index].yen_paths) {
            vector<PurificationScheme> schemes = prepare_path_schemes(path);
            if(schemes.empty()) continue;

            for(const PurificationScheme& scheme : schemes) {
                for(int start_time = 0; start_time < graph.get_time_limit(); ++start_time) {
                    Shape_vector shape_vector =
                        build_balanced_shape(path, start_time);
                    if(shape_vector.empty()) break;

                    map<ResourceKey, int> usage =
                        calculate_memory_usage(shape_vector, scheme.rounds);
                    if(!fits_initial_memory(usage)) continue;

                    Candidate candidate;
                    candidate.path = path;
                    candidate.purify_rounds = scheme.rounds;
                    candidate.shape_vector = shape_vector;
                    candidate.memory_usage = usage;
                    candidate.fidelity = evaluate_fidelity(
                        shape_vector, scheme.rounds);
                    Shape shape(shape_vector, scheme.rounds);
                    candidate.success_probability = graph.path_Pr_purify(shape);
                    candidate.start_time = start_time;
                    candidates[group_index].push_back(candidate);
                }
            }
        }
    }
}

vector<vector<int>> EFiRAP::solve_eps_with_gurobi() {
#if EFIRAP_HAS_GUROBI
    try {
        GRBEnv environment(true);
        environment.set(GRB_IntParam_OutputFlag, DEBUG ? 1 : 0);
        environment.start();

        GRBModel model(environment);
        model.set(GRB_StringAttr_ModelName, "EFiRAP_EPS");
        model.set(GRB_DoubleParam_MIPGap, mip_gap);
        if(solver_time_limit_seconds > 0.0) {
            model.set(GRB_DoubleParam_TimeLimit, solver_time_limit_seconds);
        }

        vector<vector<GRBVar>> variables(candidates.size());
        GRBLinExpr objective = 0.0;
        for(size_t group = 0; group < candidates.size(); ++group) {
            variables[group].reserve(candidates[group].size());
            for(size_t candidate = 0; candidate < candidates[group].size(); ++candidate) {
                string name = "x_" + to_string(group) + "_" +
                              to_string(candidate);
                GRBVar variable = model.addVar(
                    0.0,
                    request_groups[group].demand,
                    1.0,
                    GRB_INTEGER,
                    name);
                variables[group].push_back(variable);
                objective += variable;
            }
        }
        model.setObjective(objective, GRB_MAXIMIZE);

        // Each SD pair cannot receive more connections than requested.
        for(size_t group = 0; group < candidates.size(); ++group) {
            GRBLinExpr admitted = 0.0;
            for(GRBVar& variable : variables[group]) admitted += variable;
            model.addConstr(
                admitted <= request_groups[group].demand,
                "demand_" + to_string(group));
        }

        // Adapted Problem (12b): per-node, per-time quantum memory.
        for(int node = 0; node < graph.get_num_nodes(); ++node) {
            for(int time = 0; time < graph.get_time_limit(); ++time) {
                GRBLinExpr consumed = 0.0;
                for(size_t group = 0; group < candidates.size(); ++group) {
                    for(size_t candidate = 0;
                        candidate < candidates[group].size();
                        ++candidate) {
                        auto it = candidates[group][candidate].memory_usage.find(
                            {node, time});
                        if(it != candidates[group][candidate].memory_usage.end()) {
                            consumed += it->second * variables[group][candidate];
                        }
                    }
                }
                model.addConstr(
                    consumed <= graph.get_node_memory_at(node, time),
                    "memory_" + to_string(node) + "_" + to_string(time));
            }
        }

        model.optimize();
        int solution_count = model.get(GRB_IntAttr_SolCount);
        if(solution_count <= 0) {
            throw runtime_error("Gurobi found no feasible EFiRAP EPS solution");
        }

        int status = model.get(GRB_IntAttr_Status);
        if(status != GRB_OPTIMAL && status != GRB_TIME_LIMIT &&
           status != GRB_SUBOPTIMAL && status != GRB_INTERRUPTED) {
            throw runtime_error(
                "Gurobi stopped with status " + to_string(status));
        }

        vector<vector<int>> allocation(candidates.size());
        for(size_t group = 0; group < candidates.size(); ++group) {
            allocation[group].resize(candidates[group].size(), 0);
            for(size_t candidate = 0;
                candidate < candidates[group].size();
                ++candidate) {
                double value = variables[group][candidate].get(GRB_DoubleAttr_X);
                allocation[group][candidate] =
                    max(0, (int)llround(value));
            }
        }

        res["efirap_eps_objective"] = model.get(GRB_DoubleAttr_ObjVal);
        res["efirap_eps_bound"] = model.get(GRB_DoubleAttr_ObjBound);
        return allocation;
    } catch(const GRBException& error) {
        throw runtime_error(
            "EFiRAP Gurobi error " + to_string(error.getErrorCode()) +
            ": " + error.getMessage());
    }
#else
    throw runtime_error(
        "EFiRAP EPS requires Gurobi. Recompile EFiRAP.cpp with "
        "-DEFIRAP_USE_GUROBI, add Gurobi's include path, and link "
        "gurobi_c++ plus the installed Gurobi version library.");
#endif
}

void EFiRAP::reserve_candidate(const Candidate& candidate) {
    if(!fits_current_memory(candidate.memory_usage)) {
        throw runtime_error(
            "EFiRAP EPS returned an allocation that exceeds current memory");
    }

    Shape shape(candidate.shape_vector, candidate.purify_rounds);
    if(!graph.check_resource(shape, true, true)) {
        throw runtime_error(
            "EFiRAP candidate failed fidelity or base-memory validation");
    }

    // reserve_shape accounts for the primary Bell pairs and records metrics.
    graph.reserve_shape(shape, true);

    // Add the simultaneous sacrificial-pair memory omitted by reserve_shape.
    // It is reserved only while purification runs and is reusable afterward.
    for(size_t link = 0; link + 1 < candidate.shape_vector.size(); ++link) {
        int rounds = link < candidate.purify_rounds.size()
                         ? candidate.purify_rounds[link]
                         : 0;
        if(rounds <= 0) continue;

        int generation_time =
            candidate.shape_vector[link].second.back().first;
        int left_node = candidate.shape_vector[link].first;
        int right_node = candidate.shape_vector[link + 1].first;
        graph.reserve_node_memory_at(left_node, generation_time, rounds);
        graph.reserve_node_memory_at(right_node, generation_time, rounds);
    }
}

void EFiRAP::run() {
    cerr << "[" << algorithm_name << "] start (Yen K=" << k_paths << ")"
         << endl;

    build_request_groups();
    prepare_candidates();

    int path_count = 0;
    int candidate_count = 0;
    for(const RequestGroup& group : request_groups) {
        path_count += (int)group.yen_paths.size();
    }
    for(const vector<Candidate>& group_candidates : candidates) {
        candidate_count += (int)group_candidates.size();
    }
    res["efirap_path_cnt"] = path_count;
    res["efirap_candidate_cnt"] = candidate_count;

    if(candidate_count > 0 && !requests.empty()) {
        vector<vector<int>> allocation = solve_eps_with_gurobi();
        for(size_t group = 0; group < allocation.size(); ++group) {
            for(size_t candidate = 0;
                candidate < allocation[group].size();
                ++candidate) {
                for(int copy = 0; copy < allocation[group][candidate]; ++copy) {
                    reserve_candidate(candidates[group][candidate]);
                }
            }
        }
    }

    update_res();
    if(initial_memory_capacity > 0) {
        res["utilization"] =
            (double)graph.get_usage() / (double)initial_memory_capacity;
    } else {
        res["utilization"] = 0.0;
    }
    res["efirap_path_cnt"] = path_count;
    res["efirap_candidate_cnt"] = candidate_count;

    cerr << "[" << algorithm_name << "] end: paths=" << path_count
         << " candidates=" << candidate_count
         << " admitted=" << graph.get_actual_req_cnt() << endl;
}
