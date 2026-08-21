#include "EFiRAP.h"

#include <cmath>
#include <stdexcept>
#include <tuple>

#ifdef EFIRAP_USE_GUROBI
#include "gurobi_c++.h"
#define EFIRAP_HAS_GUROBI 1
#else
#define EFIRAP_HAS_GUROBI 0
#endif

namespace {

constexpr double MARGINAL_TIE_EPS = 1e-10;
constexpr int MAX_PURIFICATION_ROUNDS = 3;

} // namespace

EFiRAP::EFiRAP(const Graph& graph,
               const vector<SDpair>& requests,
               const map<SDpair, vector<Path>>& paths,
               double approximation_epsilon,
               double solver_time_limit_seconds,
               long long enumeration_state_limit)
    : AlgorithmBase(graph, requests, paths),
      approximation_epsilon(approximation_epsilon),
      solver_time_limit_seconds(solver_time_limit_seconds),
      enumeration_state_limit(enumeration_state_limit) {
    if(approximation_epsilon <= 0.0 || approximation_epsilon >= 1.0) {
        throw invalid_argument(
            "EFiRAP requires 0 < approximation_epsilon < 1");
    }
    if(solver_time_limit_seconds < 0.0) {
        throw invalid_argument("EFiRAP requires a non-negative solver time limit");
    }
    if(enumeration_state_limit < 0) {
        throw invalid_argument(
            "EFiRAP requires a non-negative enumeration state limit");
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
        const auto path_it = paths.find(group.sd);
        if(path_it != paths.end()) group.shared_paths = path_it->second;
        request_groups.push_back(group);
    }
}

int EFiRAP::assign_balanced_swap_times(
    int left,
    int right,
    int start_time,
    const vector<int>& purify_rounds,
    vector<int>& swap_time) const {
    if(right == left + 1) {
        // WPFA needs rounds + 1 time units to prepare a purified elementary
        // link. Its inclusive Shape lifetime is therefore rounds + 2 slots.
        return start_time + purify_rounds[left] + 1;
    }

    int middle = (left + right) / 2;
    int left_completion =
        assign_balanced_swap_times(
            left, middle, start_time, purify_rounds, swap_time);
    int right_completion =
        assign_balanced_swap_times(
            middle, right, start_time, purify_rounds, swap_time);
    swap_time[middle] = max(left_completion, right_completion);
    return swap_time[middle] + 1;
}

Shape_vector EFiRAP::build_balanced_shape(
    const Path& path,
    const vector<int>& purify_rounds,
    int start_time) {
    if(path.size() < 2 || start_time < 0) return {};
    if(purify_rounds.size() + 1 != path.size()) return {};
    for(int rounds : purify_rounds) {
        if(rounds < 0 || rounds > MAX_PURIFICATION_ROUNDS) return {};
    }

    vector<int> swap_time(path.size(), -1);
    int completion = assign_balanced_swap_times(
        0,
        (int)path.size() - 1,
        start_time,
        purify_rounds,
        swap_time);
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

    // EFiRAP generates all sacrificial pairs simultaneously. Their memory
    // cells stay occupied through the end of the simulation horizon.
    for(size_t link = 0; link + 1 < shape_vector.size(); ++link) {
        int rounds = link < purify_rounds.size() ? purify_rounds[link] : 0;
        if(rounds <= 0) continue;

        int link_start = shape_vector[link].second.back().first;
        int left_node = shape_vector[link].first;
        int right_node = shape_vector[link + 1].first;
        for(int t = link_start; t < time_limit; ++t) {
            usage[{left_node, t}] += rounds;
            usage[{right_node, t}] += rounds;
        }
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
    const int link_count = (int)path.size() - 1;
    queue<vector<int>> frontier;
    set<vector<int>> visited;
    frontier.push(vector<int>(link_count, 0));

    while(!frontier.empty()) {
        vector<int> rounds = frontier.front();
        frontier.pop();
        if(!visited.insert(rounds).second) continue;

        Shape_vector shape_vector = build_balanced_shape(path, rounds, 0);
        if(shape_vector.empty()) continue;

        map<ResourceKey, int> usage =
            calculate_memory_usage(shape_vector, rounds);
        if(!fits_initial_memory(usage)) continue;

        double fidelity = evaluate_fidelity(shape_vector, rounds);
        if(fidelity >= graph.get_fidelity_threshold()) {
            result.push_back({rounds, fidelity});
            continue;
        }

        double best_gain = 0.0;
        vector<pair<vector<int>, double>> next_states;
        for(int link = 0; link < link_count; ++link) {
            if(rounds[link] >= MAX_PURIFICATION_ROUNDS) continue;

            vector<int> next = rounds;
            next[link]++;
            Shape_vector next_shape = build_balanced_shape(path, next, 0);
            if(next_shape.empty()) continue;

            map<ResourceKey, int> next_usage =
                calculate_memory_usage(next_shape, next);
            if(!fits_initial_memory(next_usage)) continue;

            double next_fidelity = evaluate_fidelity(next_shape, next);
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
        for(const Path& path : request_groups[group_index].shared_paths) {
            vector<PurificationScheme> schemes = prepare_path_schemes(path);
            if(schemes.empty()) continue;

            for(const PurificationScheme& scheme : schemes) {
                for(int start_time = 0; start_time < graph.get_time_limit(); ++start_time) {
                    Shape_vector shape_vector =
                        build_balanced_shape(
                            path, scheme.rounds, start_time);
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

vector<vector<int>> EFiRAP::solve_eps_ptas_with_gurobi() {
#if EFIRAP_HAS_GUROBI
    try {
        GRBEnv environment(true);
        environment.set(GRB_IntParam_OutputFlag, DEBUG ? 1 : 0);
        environment.start();

        GRBModel model(environment);
        model.set(GRB_StringAttr_ModelName, "EFiRAP_EPS_PTAS_LP");
        if(solver_time_limit_seconds > 0.0) {
            model.set(GRB_DoubleParam_TimeLimit, solver_time_limit_seconds);
        }

        struct FlatCandidate {
            size_t group;
            size_t candidate;
        };

        const int node_count = graph.get_num_nodes();
        const int time_count = graph.get_time_limit();
        const int resource_count = node_count * time_count;

        vector<FlatCandidate> flat_candidates;
        vector<vector<int>> flat_indices(candidates.size());
        for(size_t group = 0; group < candidates.size(); ++group) {
            flat_indices[group].resize(candidates[group].size(), -1);
            for(size_t candidate = 0;
                candidate < candidates[group].size();
                ++candidate) {
                flat_indices[group][candidate] =
                    (int)flat_candidates.size();
                flat_candidates.push_back({group, candidate});
            }
        }

        const int variable_count = (int)flat_candidates.size();
        vector<vector<pair<int, int>>> usage_by_variable(variable_count);
        vector<vector<pair<int, int>>> variables_by_resource(resource_count);
        vector<int> resource_capacity(resource_count, 0);
        for(int node = 0; node < node_count; ++node) {
            for(int time = 0; time < time_count; ++time) {
                resource_capacity[node * time_count + time] =
                    graph.get_node_memory_at(node, time);
            }
        }

        for(int flat = 0; flat < variable_count; ++flat) {
            const FlatCandidate& index = flat_candidates[flat];
            for(const auto& entry :
                candidates[index.group][index.candidate].memory_usage) {
                int resource = entry.first.first * time_count +
                               entry.first.second;
                usage_by_variable[flat].push_back(
                    {resource, entry.second});
                variables_by_resource[resource].push_back(
                    {flat, entry.second});
            }
        }

        vector<int> variable_upper_bound(variable_count, 0);
        for(int flat = 0; flat < variable_count; ++flat) {
            const FlatCandidate& index = flat_candidates[flat];
            int upper = request_groups[index.group].demand;
            for(const auto& entry : usage_by_variable[flat]) {
                if(entry.second > 0) {
                    upper = min(
                        upper,
                        resource_capacity[entry.first] / entry.second);
                }
            }
            variable_upper_bound[flat] = upper;
        }

        vector<GRBVar> variables;
        variables.reserve(variable_count);
        GRBLinExpr objective = 0.0;
        for(int flat = 0; flat < variable_count; ++flat) {
            const FlatCandidate& index = flat_candidates[flat];
            string name = "x_" + to_string(index.group) + "_" +
                          to_string(index.candidate);
            variables.push_back(model.addVar(
                0.0,
                variable_upper_bound[flat],
                1.0,
                GRB_CONTINUOUS,
                name));
            objective += variables.back();
        }
        model.setObjective(objective, GRB_MAXIMIZE);

        int constraint_count = 0;
        // Simulator adaptation: each SD pair cannot receive more connections
        // than requested. Count these constraints in Algorithm 2's m.
        for(size_t group = 0; group < candidates.size(); ++group) {
            if(candidates[group].empty()) continue;
            GRBLinExpr admitted = 0.0;
            for(size_t candidate = 0;
                candidate < candidates[group].size();
                ++candidate) {
                admitted += variables[flat_indices[group][candidate]];
            }
            model.addConstr(
                admitted <= request_groups[group].demand,
                "demand_" + to_string(group));
            constraint_count++;
        }

        // Adapted Problem (12b): per-node, per-time quantum memory.
        for(int resource = 0; resource < resource_count; ++resource) {
            if(variables_by_resource[resource].empty()) continue;

            GRBLinExpr consumed = 0.0;
            for(const auto& entry : variables_by_resource[resource]) {
                consumed += entry.second * variables[entry.first];
            }
            int node = resource / time_count;
            int time = resource % time_count;
            model.addConstr(
                consumed <= resource_capacity[resource],
                "memory_" + to_string(node) + "_" + to_string(time));
            constraint_count++;
        }

        // Algorithm 2, Lines 1-2: solve the continuous LP relaxation.
        model.optimize();
        int status = model.get(GRB_IntAttr_Status);
        if(status != GRB_OPTIMAL) {
            throw runtime_error(
                "EFiRAP EPS LP relaxation was not solved to optimality; "
                "Gurobi status " + to_string(status));
        }

        const double lp_objective = model.get(GRB_DoubleAttr_ObjVal);
        vector<double> initial_lp(variable_count, 0.0);
        vector<int> best_flat(variable_count, 0);
        int best_objective = 0;
        for(int flat = 0; flat < variable_count; ++flat) {
            initial_lp[flat] = variables[flat].get(GRB_DoubleAttr_X);
            best_flat[flat] = max(
                0,
                min(variable_upper_bound[flat],
                    (int)floor(initial_lp[flat] + 1e-7)));
            best_objective += best_flat[flat];
        }
        const int initial_floor_objective = best_objective;

        // Algorithm 2, Line 3. The ceiling is the form used in the theorem's
        // proof; floor(z_hat) also bounds the number of admitted requests.
        double threshold_value =
            constraint_count * (1.0 - approximation_epsilon) /
            approximation_epsilon;
        int lp_floor = max(0, (int)floor(lp_objective + 1e-7));
        int approximation_threshold = lp_floor;
        if(threshold_value < lp_floor) {
            approximation_threshold =
                (int)ceil(max(0.0, threshold_value) - 1e-12);
        }
        int enumeration_target = min(lp_floor, approximation_threshold);

        vector<int> variable_order(variable_count, 0);
        for(int flat = 0; flat < variable_count; ++flat) {
            variable_order[flat] = flat;
        }
        sort(variable_order.begin(), variable_order.end(),
             [&](int left, int right) {
                 if(fabs(initial_lp[left] - initial_lp[right]) > 1e-12) {
                     return initial_lp[left] > initial_lp[right];
                 }
                 int left_usage = 0;
                 int right_usage = 0;
                 for(const auto& entry : usage_by_variable[left]) {
                     left_usage += entry.second;
                 }
                 for(const auto& entry : usage_by_variable[right]) {
                     right_usage += entry.second;
                 }
                 if(left_usage != right_usage) {
                     return left_usage < right_usage;
                 }
                 return left < right;
             });

        vector<long long> suffix_upper(variable_count + 1, 0);
        for(int position = variable_count - 1; position >= 0; --position) {
            suffix_upper[position] = min<long long>(
                requests.size(),
                suffix_upper[position + 1] +
                    variable_upper_bound[variable_order[position]]);
        }

        long long enumeration_states = 0;
        long long lp_subproblems = 0;
        bool enumeration_truncated = false;
        vector<int> seed(variable_count, 0);
        vector<int> used_by_group(candidates.size(), 0);
        vector<int> used_by_resource(resource_count, 0);

        auto solve_seed_lp = [&]() {
            for(int flat = 0; flat < variable_count; ++flat) {
                variables[flat].set(GRB_DoubleAttr_LB, seed[flat]);
            }
            model.optimize();
            lp_subproblems++;

            int seed_status = model.get(GRB_IntAttr_Status);
            if(seed_status == GRB_INFEASIBLE ||
               seed_status == GRB_INF_OR_UNBD) {
                return false;
            }
            if(seed_status != GRB_OPTIMAL) {
                throw runtime_error(
                    "EFiRAP EPS constrained LP was not solved to optimality; "
                    "Gurobi status " + to_string(seed_status));
            }

            vector<int> rounded(variable_count, 0);
            int rounded_objective = 0;
            for(int flat = 0; flat < variable_count; ++flat) {
                int value = (int)floor(
                    variables[flat].get(GRB_DoubleAttr_X) + 1e-7);
                value = max(seed[flat], value);
                value = min(variable_upper_bound[flat], value);
                rounded[flat] = value;
                rounded_objective += value;
            }
            if(rounded_objective > best_objective) {
                best_objective = rounded_objective;
                best_flat.swap(rounded);
            }
            return true;
        };

        // Algorithm 2, Lines 5-9, with the paper's Section III-E pruning:
        // reject resource-infeasible guesses before solving their LP and move
        // to t+1 after the first feasible guess for the current t.
        for(int target = initial_floor_objective + 1;
            target <= enumeration_target && !enumeration_truncated;
            ++target) {
            fill(seed.begin(), seed.end(), 0);
            fill(used_by_group.begin(), used_by_group.end(), 0);
            fill(used_by_resource.begin(), used_by_resource.end(), 0);

            function<bool(int, int)> enumerate_guess =
                [&](int position, int remaining) -> bool {
                enumeration_states++;
                if(enumeration_state_limit > 0 &&
                   enumeration_states > enumeration_state_limit) {
                    enumeration_truncated = true;
                    return false;
                }
                if(remaining == 0) return solve_seed_lp();
                if(position >= variable_count) return false;
                if(suffix_upper[position] < remaining) return false;

                int flat = variable_order[position];
                size_t group = flat_candidates[flat].group;
                int maximum = min(
                    remaining,
                    variable_upper_bound[flat]);
                maximum = min(
                    maximum,
                    request_groups[group].demand - used_by_group[group]);
                for(const auto& entry : usage_by_variable[flat]) {
                    if(entry.second <= 0) continue;
                    maximum = min(
                        maximum,
                        (resource_capacity[entry.first] -
                         used_by_resource[entry.first]) /
                            entry.second);
                }

                for(int value = maximum; value >= 0; --value) {
                    seed[flat] = value;
                    used_by_group[group] += value;
                    for(const auto& entry : usage_by_variable[flat]) {
                        used_by_resource[entry.first] +=
                            value * entry.second;
                    }

                    bool found = enumerate_guess(
                        position + 1, remaining - value);

                    used_by_group[group] -= value;
                    for(const auto& entry : usage_by_variable[flat]) {
                        used_by_resource[entry.first] -=
                            value * entry.second;
                    }
                    seed[flat] = 0;

                    if(found || enumeration_truncated) return found;
                }
                return false;
            };

            bool found = enumerate_guess(0, target);
            if(!found && !enumeration_truncated) {
                // No feasible integer lower-bound guess of this cardinality;
                // larger cardinalities cannot be feasible either.
                break;
            }
        }

        vector<vector<int>> allocation(candidates.size());
        for(size_t group = 0; group < candidates.size(); ++group) {
            allocation[group].resize(candidates[group].size(), 0);
            for(size_t candidate = 0;
                candidate < candidates[group].size();
                ++candidate) {
                allocation[group][candidate] =
                    best_flat[flat_indices[group][candidate]];
            }
        }

        res["efirap_eps_objective"] = best_objective;
        res["efirap_eps_bound"] = lp_objective;
        res["efirap_eps_epsilon"] = approximation_epsilon;
        res["efirap_eps_constraint_count"] = constraint_count;
        res["efirap_eps_floor_objective"] = initial_floor_objective;
        res["efirap_eps_enumeration_target"] = enumeration_target;
        res["efirap_eps_enumeration_states"] =
            (double)enumeration_states;
        res["efirap_eps_lp_subproblems"] = (double)lp_subproblems;
        res["efirap_eps_truncated"] = enumeration_truncated ? 1.0 : 0.0;

        cerr << "[" << algorithm_name << "] EPS Algorithm 2: epsilon="
             << approximation_epsilon << " lp_bound=" << lp_objective
             << " initial_floor=" << initial_floor_objective
             << " admitted=" << best_objective
             << " target=" << enumeration_target
             << " states=" << enumeration_states
             << " subproblems=" << lp_subproblems;
        if(enumeration_truncated) {
            cerr << " truncated_at=" << enumeration_state_limit;
        }
        cerr << endl;
        return allocation;
    } catch(const GRBException& error) {
        throw runtime_error(
            "EFiRAP Gurobi error " + to_string(error.getErrorCode()) +
            ": " + error.getMessage());
    }
#else
    throw runtime_error(
        "EFiRAP EPS PTAS requires Gurobi as its LP solver. Recompile "
        "EFiRAP.cpp with "
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

    // Reserve exactly the purification extras included in the EPS memory
    // constraints. Subtract the primary cells already handled by reserve_shape
    // so optimization and execution cannot drift apart.
    map<ResourceKey, int> primary_usage;
    for(const auto& node_ranges : candidate.shape_vector) {
        int node = node_ranges.first;
        for(const auto& range : node_ranges.second) {
            for(int time = range.first; time <= range.second; ++time) {
                primary_usage[{node, time}]++;
            }
        }
    }

    for(const auto& entry : candidate.memory_usage) {
        int primary = 0;
        auto primary_it = primary_usage.find(entry.first);
        if(primary_it != primary_usage.end()) primary = primary_it->second;

        int extra = entry.second - primary;
        if(extra > 0) {
            graph.reserve_node_memory_at(
                entry.first.first, entry.first.second, extra);
        }
    }
}

void EFiRAP::run() {
    cerr << "[" << algorithm_name << "] start (shared path set)" << endl;

    build_request_groups();
    prepare_candidates();

    int path_count = 0;
    int candidate_count = 0;
    for(const RequestGroup& group : request_groups) {
        path_count += (int)group.shared_paths.size();
    }
    for(const vector<Candidate>& group_candidates : candidates) {
        candidate_count += (int)group_candidates.size();
    }
    res["efirap_path_cnt"] = path_count;
    res["efirap_candidate_cnt"] = candidate_count;

    if(candidate_count > 0 && !requests.empty()) {
        vector<vector<int>> allocation = solve_eps_ptas_with_gurobi();
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
