// Small-scale optimality experiment for the reviewer-requested sanity check.
//
// The exact oracle deliberately has no approximation bucket, candidate cap,
// search-state cap, or wall-clock cutoff.  For every supplied simple path it
// exhaustively constructs the same LEAF/CONT/MERGE schedules used by WPFA,
// including 0--3 rounds of link purification.  A complete branch-and-bound
// search then chooses at most one schedule per request under every node-time
// memory constraint.  Consequently, a completed OPT row is a certificate of
// optimality within the simulator's discrete-time scheduling model.

// Build and run from src/:
//   make main_small_scale
//   ./main_small_scale

// The three constants below are intentionally in source code so an experiment
// configuration can be changed and archived with the executable.
constexpr double SMALL_SCALE_EPSILON = 0.10;
constexpr double SMALL_SCALE_BUCKET_EPS = 0.001;
constexpr int SMALL_SCALE_MAX_PURIFICATION_ROUNDS = 3;

#include "./config.h"
#include "Network/Graph/Graph.h"
#include "Network/Purification/Purification.h"
#include "Algorithm/AlgorithmBase/AlgorithmBase.h"
#include "Algorithm/MyAlgo1/MyAlgo1.h"
#include "Algorithm/MyAlgo3/MyAlgo3.h"
#include "Algorithm/WernerAlgo/WernerAlgo.h"
#include "Algorithm/WernerAlgo2/WernerAlgo2.h"

// WernerAlgo*.h uses a legacy macro internally.  Keep it from changing the
// types declared by this translation unit.
#ifdef double
#undef double
#endif

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <memory>
#include <stdexcept>
#include <sys/stat.h>
#include <unordered_map>

using namespace std;

namespace {

constexpr double OBJECTIVE_TOLERANCE = 1e-10;

// These physical parameters are the same defaults used by main.cpp.
constexpr double SWAP_PROBABILITY = 0.90;
constexpr double MIN_LINK_FIDELITY = 0.80;
constexpr double MAX_LINK_FIDELITY = 0.99;
constexpr double FIDELITY_THRESHOLD = 0.80;
constexpr double DECOHERENCE_A = 0.25;
constexpr double DECOHERENCE_B = 0.75;
constexpr double DECOHERENCE_N = 2.0;
constexpr double DECOHERENCE_T = 0.04;
constexpr double SLOT_DURATION = 0.002;
constexpr double Z_MIN = 0.02702867239;
constexpr double TIME_ETA = 0.001;
constexpr double DELTA_P = 0.01;
constexpr double ENTANGLE_LAMBDA = 0.045;
constexpr double ENTANGLE_TIME = 0.00025;

// Identical to WernerAlgo2's purification-memory table.  Row r describes a
// link produced with r pumping rounds; its order is reversed below exactly as
// in WernerAlgo2::run().
constexpr int PURIFY_MEMORY[4][5] = {
    {1, 1, 0, 0, 0},
    {1, 2, 2, 0, 0},
    {1, 2, 3, 2, 0},
    {1, 2, 3, 3, 2},
};

struct EdgeSpec {
    int left;
    int right;
    double fidelity_ratio;
};

struct CaseSpec {
    string name;
    int node_count;
    int time_limit;
    int memory_per_node;
    vector<EdgeSpec> edges;
    vector<SDpair> requests;
};

struct Schedule {
    Shape_vector shape_vector;
    vector<int> purify_rounds;
};

struct Candidate {
    Shape_vector shape_vector;
    vector<int> purify_rounds;
    vector<unsigned char> memory_usage;
    double fidelity = 0.0;
    double success_probability = 0.0;
    double objective = 0.0;
};

struct CandidateSet {
    vector<Candidate> candidates;
    size_t enumerated_schedules = 0;
    size_t feasible_schedules = 0;
};

struct ExactResult {
    double objective = 0.0;
    double expected_requests = 0.0;
    int accepted_requests = 0;
    unsigned long long search_states = 0;
    size_t enumerated_schedules = 0;
    size_t feasible_schedules = 0;
    size_t nondominated_candidates = 0;
    vector<pair<SDpair, Candidate>> selected;
};

struct AlgorithmResult {
    string name;
    double objective = 0.0;
    double expected_requests = 0.0;
    double accepted_requests = 0.0;
    double runtime_ms = 0.0;
    // The schedules the algorithm reserved, recorded by Graph while the
    // reported objective was accumulated.
    vector<AcceptedShapeRecord> selected;
};

struct SearchGroup {
    SDpair request;
    int demand = 0;
    const vector<Candidate>* candidates = nullptr;
    double best_single_objective = 0.0;
};

const vector<CaseSpec>& experiment_cases() {
    // line3 isolates time/memory contention; line4 additionally exercises
    // non-trivial swap-tree choices; diamond4 also tests route selection.
    static const vector<CaseSpec> cases = {
        {
            "line3", 3, 5, 2,
            {{0, 1, 0.90}, {1, 2, 0.82}},
            {{0, 2}, {0, 2}, {0, 2}}
        },
        {
            "line4", 4, 6, 3,
            {{0, 1, 0.92}, {1, 2, 0.84}, {2, 3, 0.94}},
            {{0, 3}, {0, 3}, {0, 3}}
        },
        {
            "diamond4", 4, 5, 2,
            {{0, 1, 0.86}, {1, 3, 0.95},
             {0, 2, 0.95}, {2, 3, 0.80}},
            {{0, 3}, {0, 3}, {0, 3}}
        },
        {
            // Reviewer-facing canonical case.  Paper labels are 1-based:
            // (1,3), (1,4), and (2,4).  The simulator is 0-based.
            "reviewer_line4", 4, 6, 4,
            {{0, 1, 0.92}, {1, 2, 0.84}, {2, 3, 0.94}},
            {{0, 2}, {0, 3}, {1, 3}}
        },
    };
    return cases;
}

string locate_data_directory() {
    auto is_directory = [](const string& path) {
        struct stat information;
        return stat(path.c_str(), &information) == 0 &&
               (information.st_mode & S_IFDIR) != 0;
    };
    if(is_directory("../data/input") && is_directory("../data/ans")) {
        return "../data";
    }
    if(is_directory("data/input") && is_directory("data/ans")) {
        return "data";
    }
    throw runtime_error(
        "cannot locate data directory; run from the repository root or src/");
}

void write_graph_file(const string& filename, const CaseSpec& spec) {
    ofstream output(filename);
    if(!output) {
        throw runtime_error("cannot write small-scale graph: " + filename);
    }

    output << spec.node_count << '\n';
    // Graph adds avg_memory to each value in the file.  Zero therefore gives
    // every node exactly memory_per_node cells in every time slot.
    for(int node = 0; node < spec.node_count; ++node) output << "0\n";
    output << spec.edges.size() << '\n';
    output << setprecision(17);
    for(const EdgeSpec& edge : spec.edges) {
        output << edge.left << ' ' << edge.right << ' '
               << edge.fidelity_ratio << '\n';
    }
}

Graph load_graph(const string& filename, const CaseSpec& spec) {
    return Graph(
        filename, spec.time_limit, SWAP_PROBABILITY,
        spec.memory_per_node, MIN_LINK_FIDELITY, MAX_LINK_FIDELITY,
        FIDELITY_THRESHOLD, DECOHERENCE_A, DECOHERENCE_B,
        DECOHERENCE_N, DECOHERENCE_T, SLOT_DURATION, Z_MIN,
        SMALL_SCALE_BUCKET_EPS, TIME_ETA, DELTA_P,
        ENTANGLE_LAMBDA, ENTANGLE_TIME);
}

void enumerate_paths_dfs(const Graph& graph,
                         int current,
                         int destination,
                         vector<unsigned char>& visited,
                         Path& path,
                         vector<Path>& result) {
    if(current == destination) {
        result.push_back(path);
        return;
    }
    for(int next : graph.adj_list[current]) {
        if(visited[next]) continue;
        visited[next] = 1;
        path.push_back(next);
        enumerate_paths_dfs(
            graph, next, destination, visited, path, result);
        path.pop_back();
        visited[next] = 0;
    }
}

vector<Path> enumerate_all_simple_paths(const Graph& graph,
                                        int source,
                                        int destination) {
    vector<Path> paths;
    vector<unsigned char> visited(graph.adj_list.size(), 0);
    Path path = {source};
    visited[source] = 1;
    enumerate_paths_dfs(
        graph, source, destination, visited, path, paths);
    sort(paths.begin(), paths.end(), [](const Path& left, const Path& right) {
        if(left.size() != right.size()) return left.size() < right.size();
        return left < right;
    });
    return paths;
}

map<SDpair, vector<Path>> build_all_paths(
    const Graph& graph, const vector<SDpair>& requests) {
    map<SDpair, vector<Path>> paths;
    for(const SDpair& request : requests) {
        if(paths.count(request)) continue;
        paths[request] = enumerate_all_simple_paths(
            graph, request.first, request.second);
        if(paths[request].empty()) {
            throw runtime_error(
                "small-scale request has no path: " +
                to_string(request.first) + "->" +
                to_string(request.second));
        }
    }
    return paths;
}

Schedule make_leaf(const Path& path, int edge, int end_time, int rounds) {
    const int start_time = end_time - rounds - 1;
    Schedule result;
    result.shape_vector = {
        {path[edge], {{start_time, end_time}}},
        {path[edge + 1], {{start_time, end_time}}}
    };
    result.purify_rounds = {rounds};
    return result;
}

Schedule continue_schedule(const Schedule& previous) {
    Schedule result = previous;
    result.shape_vector.front().second.front().second++;
    result.shape_vector.back().second.front().second++;
    return result;
}

Schedule merge_schedules(const Schedule& left, const Schedule& right) {
    Schedule result;
    result.shape_vector = left.shape_vector;
    result.shape_vector.back().second.push_back(
        right.shape_vector.front().second.front());
    result.shape_vector.insert(
        result.shape_vector.end(),
        right.shape_vector.begin() + 1,
        right.shape_vector.end());
    result.shape_vector.front().second.front().second++;
    result.shape_vector.back().second.front().second++;

    result.purify_rounds = left.purify_rounds;
    result.purify_rounds.insert(
        result.purify_rounds.end(),
        right.purify_rounds.begin(),
        right.purify_rounds.end());
    return result;
}

void deduplicate_schedules(vector<Schedule>& schedules) {
    using Key = pair<Shape_vector, vector<int>>;
    set<Key> seen;
    vector<Schedule> unique;
    unique.reserve(schedules.size());
    for(Schedule& schedule : schedules) {
        Key key = {schedule.shape_vector, schedule.purify_rounds};
        if(seen.insert(key).second) unique.push_back(std::move(schedule));
    }
    schedules.swap(unique);
}

vector<Schedule> enumerate_path_schedules(const Path& path, int time_limit) {
    const int node_count = (int)path.size();
    using Cell = vector<Schedule>;
    vector<vector<vector<Cell>>> table(
        time_limit,
        vector<vector<Cell>>(node_count, vector<Cell>(node_count)));

    for(int time = 1; time < time_limit; ++time) {
        for(int length = 1; length < node_count; ++length) {
            for(int left = 0; left + length < node_count; ++left) {
                const int right = left + length;
                Cell& cell = table[time][left][right];

                if(length == 1) {
                    for(int rounds = 0;
                        rounds <= SMALL_SCALE_MAX_PURIFICATION_ROUNDS;
                        ++rounds) {
                        if(time - rounds <= 0) continue;
                        cell.push_back(make_leaf(path, left, time, rounds));
                    }
                }

                for(const Schedule& previous : table[time - 1][left][right]) {
                    cell.push_back(continue_schedule(previous));
                }

                for(int middle = left + 1; middle < right; ++middle) {
                    const Cell& left_cell = table[time - 1][left][middle];
                    const Cell& right_cell = table[time - 1][middle][right];
                    for(const Schedule& left_schedule : left_cell) {
                        for(const Schedule& right_schedule : right_cell) {
                            cell.push_back(merge_schedules(
                                left_schedule, right_schedule));
                        }
                    }
                }
                deduplicate_schedules(cell);
            }
        }
    }

    vector<Schedule> result;
    for(int time = 1; time < time_limit; ++time) {
        const Cell& cell = table[time][0][node_count - 1];
        result.insert(result.end(), cell.begin(), cell.end());
    }
    deduplicate_schedules(result);
    return result;
}

// theta_m(t, v) of one schedule, flattened as node * time_limit + time.
// This is the numerology footprint of the paper: one unit for every memory
// cell the schedule holds at a node in a slot, including the auxiliary pairs
// that edge-local pumping needs.  Returns false only when the schedule
// addresses a node or slot outside the batch.
bool compute_theta(int node_count,
                   int time_limit,
                   const Shape_vector& shape_vector,
                   const vector<int>& purify_rounds,
                   vector<int>& theta) {
    theta.assign((size_t)node_count * time_limit, 0);
    bool inside_batch = true;

    auto add = [&](int node, int time, int amount) {
        if(node < 0 || node >= node_count || time < 0 || time >= time_limit) {
            inside_batch = false;
            return;
        }
        theta[(size_t)node * time_limit + time] += amount;
    };

    for(const auto& node_ranges : shape_vector) {
        for(const auto& range : node_ranges.second) {
            for(int time = range.first; time <= range.second; ++time) {
                add(node_ranges.first, time, 1);
            }
        }
    }

    for(size_t link = 0; link + 1 < shape_vector.size(); ++link) {
        const int rounds =
            link < purify_rounds.size() ? purify_rounds[link] : 0;
        if(rounds <= 0) continue;
        const int link_start = shape_vector[link].second.back().first;
        const int left_node = shape_vector[link].first;
        const int right_node = shape_vector[link + 1].first;
        for(int offset = 0; offset <= rounds + 1; ++offset) {
            const int extra =
                PURIFY_MEMORY[rounds][rounds + 1 - offset] - 1;
            if(extra <= 0) continue;
            add(left_node, link_start + offset, extra);
            add(right_node, link_start + offset, extra);
        }
    }
    return inside_batch;
}

bool build_memory_usage(Graph& graph,
                        const Schedule& schedule,
                        vector<unsigned char>& usage) {
    const int node_count = graph.get_num_nodes();
    const int time_limit = graph.get_time_limit();
    vector<int> theta;
    if(!compute_theta(node_count, time_limit, schedule.shape_vector,
                      schedule.purify_rounds, theta)) {
        return false;
    }
    for(int node = 0; node < node_count; ++node) {
        for(int time = 0; time < time_limit; ++time) {
            const size_t index = (size_t)node * time_limit + time;
            if(theta[index] > graph.get_node_memory_at(node, time)) {
                return false;
            }
            usage[index] = (unsigned char)theta[index];
        }
    }
    return true;
}

bool dominates(const Candidate& left, const Candidate& right) {
    if(left.objective + OBJECTIVE_TOLERANCE < right.objective) return false;
    bool strictly_better = left.objective >
        right.objective + OBJECTIVE_TOLERANCE;
    for(size_t index = 0; index < left.memory_usage.size(); ++index) {
        if(left.memory_usage[index] > right.memory_usage[index]) return false;
        if(left.memory_usage[index] < right.memory_usage[index]) {
            strictly_better = true;
        }
    }
    return strictly_better ||
           left.shape_vector == right.shape_vector;
}

CandidateSet build_candidates(Graph& graph, const vector<Path>& paths) {
    CandidateSet result;
    vector<Candidate> feasible;

    for(const Path& path : paths) {
        vector<Schedule> schedules =
            enumerate_path_schedules(path, graph.get_time_limit());
        result.enumerated_schedules += schedules.size();
        for(const Schedule& schedule : schedules) {
            try {
                Shape shape(schedule.shape_vector, schedule.purify_rounds);
                const double fidelity = shape.get_fidelity(
                    graph.get_A(), graph.get_B(), graph.get_n(),
                    graph.get_T(), graph.get_tao(), graph.get_F_init(), true);
                if(fidelity + OBJECTIVE_TOLERANCE <
                   graph.get_fidelity_threshold()) {
                    continue;
                }

                Candidate candidate;
                candidate.shape_vector = schedule.shape_vector;
                candidate.purify_rounds = schedule.purify_rounds;
                candidate.memory_usage.assign(
                    graph.get_num_nodes() * graph.get_time_limit(), 0);
                if(!build_memory_usage(
                       graph, schedule, candidate.memory_usage)) {
                    continue;
                }
                candidate.fidelity = fidelity;
                candidate.success_probability = graph.path_Pr_purify(shape);
                candidate.objective =
                    Purification::fidelity_to_werner(fidelity) *
                    candidate.success_probability;
                feasible.push_back(std::move(candidate));
            } catch(const runtime_error&) {
                // A malformed schedule is not part of the feasible set.  The
                // recurrence mirrors Shape::check_valid(), so this should not
                // normally be reached; retaining the check makes the oracle
                // defensive against future Shape-model changes.
            }
        }
    }
    result.feasible_schedules = feasible.size();

    // Exact componentwise dominance: removing a schedule that uses no less
    // memory and yields no more reward cannot change any optimal solution.
    vector<unsigned char> removed(feasible.size(), 0);
    for(size_t right = 0; right < feasible.size(); ++right) {
        if(removed[right]) continue;
        for(size_t left = 0; left < feasible.size(); ++left) {
            if(left == right || removed[left]) continue;
            if(dominates(feasible[left], feasible[right])) {
                removed[right] = 1;
                break;
            }
        }
    }
    for(size_t index = 0; index < feasible.size(); ++index) {
        if(!removed[index]) {
            result.candidates.push_back(std::move(feasible[index]));
        }
    }
    sort(result.candidates.begin(), result.candidates.end(),
         [](const Candidate& left, const Candidate& right) {
             if(left.objective != right.objective) {
                 return left.objective > right.objective;
             }
             return left.memory_usage < right.memory_usage;
         });
    return result;
}

string encode_search_state(int group_index,
                           int remaining,
                           size_t minimum_candidate,
                           const vector<unsigned char>& usage) {
    string key;
    key.reserve(3 * sizeof(int) + usage.size());
    auto append_integer = [&](uint64_t value) {
        for(size_t byte = 0; byte < sizeof(value); ++byte) {
            key.push_back((char)((value >> (byte * 8)) & 0xff));
        }
    };
    append_integer((uint64_t)group_index);
    append_integer((uint64_t)remaining);
    append_integer((uint64_t)minimum_candidate);
    key.append(reinterpret_cast<const char*>(usage.data()), usage.size());
    return key;
}

class ExactSearch {
public:
    ExactSearch(Graph& graph, vector<SearchGroup> groups)
        : graph(graph), groups(std::move(groups)),
          usage(graph.get_num_nodes() * graph.get_time_limit(), 0),
          capacities(usage.size(), 0),
          suffix_upper_bound(this->groups.size() + 1, 0.0),
          current_selection(this->groups.size()) {
        for(int node = 0; node < graph.get_num_nodes(); ++node) {
            for(int time = 0; time < graph.get_time_limit(); ++time) {
                capacities[node * graph.get_time_limit() + time] =
                    (unsigned char)graph.get_node_memory_at(node, time);
            }
        }
        for(int group = (int)this->groups.size() - 1;
            group >= 0; --group) {
            suffix_upper_bound[group] = suffix_upper_bound[group + 1] +
                this->groups[group].demand *
                this->groups[group].best_single_objective;
        }
    }

    ExactResult solve() {
        search_group(0, 0.0, 0.0, 0);
        result.search_states = search_states;
        return result;
    }

private:
    Graph& graph;
    vector<SearchGroup> groups;
    vector<unsigned char> usage;
    vector<unsigned char> capacities;
    vector<double> suffix_upper_bound;
    vector<vector<size_t>> current_selection;
    vector<vector<size_t>> best_selection;
    unordered_map<string, double> best_prefix_by_state;
    ExactResult result;
    unsigned long long search_states = 0;

    bool fits(const Candidate& candidate) const {
        for(size_t index = 0; index < usage.size(); ++index) {
            if((int)usage[index] + candidate.memory_usage[index] >
               capacities[index]) {
                return false;
            }
        }
        return true;
    }

    void apply(const Candidate& candidate, int sign) {
        for(size_t index = 0; index < usage.size(); ++index) {
            usage[index] = (unsigned char)(
                (int)usage[index] + sign * candidate.memory_usage[index]);
        }
    }

    void update_best(double objective,
                     double expected_requests,
                     int accepted_requests) {
        if(objective > result.objective + OBJECTIVE_TOLERANCE ||
           (fabs(objective - result.objective) <= OBJECTIVE_TOLERANCE &&
            accepted_requests > result.accepted_requests)) {
            result.objective = objective;
            result.expected_requests = expected_requests;
            result.accepted_requests = accepted_requests;
            best_selection = current_selection;
        }
    }

    void search_group(int group_index,
                      double objective,
                      double expected_requests,
                      int accepted_requests) {
        ++search_states;
        update_best(objective, expected_requests, accepted_requests);
        if(group_index == (int)groups.size()) return;
        if(objective + suffix_upper_bound[group_index] <=
           result.objective + OBJECTIVE_TOLERANCE) {
            return;
        }

        current_selection[group_index].clear();
        search_units(group_index, groups[group_index].demand, 0,
                     objective, expected_requests, accepted_requests);
    }

    void search_units(int group_index,
                      int remaining,
                      size_t minimum_candidate,
                      double objective,
                      double expected_requests,
                      int accepted_requests) {
        ++search_states;
        const SearchGroup& group = groups[group_index];

        // Requests with the same endpoints are indistinguishable.  Candidate
        // indices are therefore chosen in nondecreasing order, enumerating
        // every multiset once instead of every permutation.
        const string state_key = encode_search_state(
            group_index, remaining, minimum_candidate, usage);
        auto memo = best_prefix_by_state.find(state_key);
        if(memo != best_prefix_by_state.end() &&
           memo->second + OBJECTIVE_TOLERANCE >= objective) {
            return;
        }
        best_prefix_by_state[state_key] = objective;

        // Stop selecting from this group; all remaining requests are rejected.
        search_group(group_index + 1, objective,
                     expected_requests, accepted_requests);
        if(remaining == 0) return;

        const double remaining_group_upper =
            remaining * group.best_single_objective +
            suffix_upper_bound[group_index + 1];
        if(objective + remaining_group_upper <=
           result.objective + OBJECTIVE_TOLERANCE) {
            return;
        }

        for(size_t index = minimum_candidate;
            index < group.candidates->size(); ++index) {
            const Candidate& candidate = (*group.candidates)[index];
            if(!fits(candidate)) continue;
            apply(candidate, +1);
            current_selection[group_index].push_back(index);
            search_units(
                group_index, remaining - 1, index,
                objective + candidate.objective,
                expected_requests + candidate.success_probability,
                accepted_requests + 1);
            current_selection[group_index].pop_back();
            apply(candidate, -1);
        }
    }

public:
    void append_selected(ExactResult& output) const {
        for(size_t group = 0; group < groups.size(); ++group) {
            for(size_t candidate_index : best_selection[group]) {
                output.selected.push_back({
                    groups[group].request,
                    (*groups[group].candidates)[candidate_index]
                });
            }
        }
    }
};

ExactResult solve_exact(Graph& graph,
                        const vector<SDpair>& requests,
                        const map<SDpair, vector<Path>>& paths) {
    map<SDpair, int> demand;
    for(const SDpair& request : requests) demand[request]++;

    map<SDpair, CandidateSet> candidate_sets;
    vector<SearchGroup> groups;
    ExactResult result;
    for(const auto& entry : demand) {
        CandidateSet set = build_candidates(graph, paths.at(entry.first));
        result.enumerated_schedules += set.enumerated_schedules;
        result.feasible_schedules += set.feasible_schedules;
        result.nondominated_candidates += set.candidates.size();
        if(set.candidates.empty()) {
            cerr << "[OPT] warning: no feasible schedule for "
                 << entry.first.first << "->" << entry.first.second << '\n';
        }
        auto inserted = candidate_sets.emplace(
            entry.first, std::move(set));
        const vector<Candidate>& candidates = inserted.first->second.candidates;
        SearchGroup group;
        group.request = entry.first;
        group.demand = entry.second;
        group.candidates = &candidates;
        if(!candidates.empty()) {
            group.best_single_objective = candidates.front().objective;
        }
        groups.push_back(group);
    }

    // Harder/high-value groups first tightens the exact bound earlier without
    // changing the explored feasible set.
    sort(groups.begin(), groups.end(), [](const SearchGroup& left,
                                          const SearchGroup& right) {
        const double left_bound =
            left.demand * left.best_single_objective;
        const double right_bound =
            right.demand * right.best_single_objective;
        if(left_bound != right_bound) return left_bound > right_bound;
        return left.request < right.request;
    });

    ExactSearch search(graph, groups);
    ExactResult optimum = search.solve();
    search.append_selected(optimum);
    optimum.enumerated_schedules = result.enumerated_schedules;
    optimum.feasible_schedules = result.feasible_schedules;
    optimum.nondominated_candidates = result.nondominated_candidates;
    return optimum;
}

AlgorithmResult run_algorithm(unique_ptr<AlgorithmBase> algorithm) {
    AlgorithmResult result;
    result.name = algorithm->get_name();
    algorithm->set_record_accepted_shapes(true);
    const auto start = chrono::steady_clock::now();
    algorithm->run();
    const auto finish = chrono::steady_clock::now();
    result.runtime_ms = chrono::duration<double, milli>(finish - start).count();
    result.objective = algorithm->get_res("fidelity_gain");
    result.expected_requests = algorithm->get_res("succ_request_cnt");
    result.accepted_requests = algorithm->get_res("actual_req_cnt");
    result.selected = algorithm->get_accepted_shapes();
    return result;
}

vector<AlgorithmResult> run_algorithms(
    const Graph& graph,
    const vector<SDpair>& requests,
    const map<SDpair, vector<Path>>& paths) {
    vector<AlgorithmResult> results;
    {
        unique_ptr<WernerAlgo2> algorithm(new WernerAlgo2(
            graph, requests, paths,
            SMALL_SCALE_EPSILON, SMALL_SCALE_BUCKET_EPS));
        algorithm->set_detailed_logging(false);
        results.push_back(run_algorithm(std::move(algorithm)));
    }
    results.push_back(run_algorithm(unique_ptr<AlgorithmBase>(
        new WernerAlgo(graph, requests, paths))));
    results.push_back(run_algorithm(unique_ptr<AlgorithmBase>(
        new MyAlgo1(graph, requests, paths))));
    results.push_back(run_algorithm(unique_ptr<AlgorithmBase>(
        new MyAlgo3(graph, requests, paths))));
    return results;
}

string path_string(const Shape_vector& shape_vector) {
    ostringstream output;
    for(size_t index = 0; index < shape_vector.size(); ++index) {
        if(index) output << '-';
        output << shape_vector[index].first;
    }
    return output.str();
}

string rounds_string(const vector<int>& rounds) {
    ostringstream output;
    for(size_t index = 0; index < rounds.size(); ++index) {
        if(index) output << '-';
        output << rounds[index];
    }
    return output.str();
}

string timing_string(const Shape_vector& shape_vector) {
    ostringstream output;
    for(size_t node_index = 0;
        node_index < shape_vector.size(); ++node_index) {
        if(node_index) output << ';';
        output << shape_vector[node_index].first << ":[";
        const auto& ranges = shape_vector[node_index].second;
        for(size_t range_index = 0;
            range_index < ranges.size(); ++range_index) {
            if(range_index) output << '|';
            output << ranges[range_index].first << '-'
                   << ranges[range_index].second;
        }
        output << ']';
    }
    return output.str();
}

string request_pairs_string(const vector<SDpair>& requests) {
    ostringstream output;
    for(size_t index = 0; index < requests.size(); ++index) {
        if(index) output << ';';
        // The paper and figure use conventional 1-based node labels.
        output << requests[index].first + 1 << '-'
               << requests[index].second + 1;
    }
    return output.str();
}

// ---------------------------------------------------------------------------
// Per-request reporting shared by OPT and by every heuristic.  The paper uses
// 1-based node labels, so the strings below are emitted in that convention
// while the CSV also keeps the 0-based simulator indices.
// ---------------------------------------------------------------------------

struct SelectionEntry {
    string algorithm;
    int src = -1;
    int dst = -1;
    Shape_vector shape_vector;
    vector<int> purify_rounds;
    double fidelity = 0.0;
    double success_probability = 0.0;
    double expected_werner = 0.0;
};

bool selection_order(const SelectionEntry& left, const SelectionEntry& right) {
    if(left.src != right.src) return left.src < right.src;
    if(left.dst != right.dst) return left.dst < right.dst;
    return left.shape_vector < right.shape_vector;
}

vector<SelectionEntry> exact_entries(const ExactResult& result) {
    vector<SelectionEntry> entries;
    for(const auto& item : result.selected) {
        SelectionEntry entry;
        entry.algorithm = "OPT";
        entry.src = item.first.first;
        entry.dst = item.first.second;
        entry.shape_vector = item.second.shape_vector;
        entry.purify_rounds = item.second.purify_rounds;
        entry.fidelity = item.second.fidelity;
        entry.success_probability = item.second.success_probability;
        entry.expected_werner = item.second.objective;
        entries.push_back(entry);
    }
    sort(entries.begin(), entries.end(), selection_order);
    return entries;
}

vector<SelectionEntry> algorithm_entries(const AlgorithmResult& result) {
    vector<SelectionEntry> entries;
    for(const AcceptedShapeRecord& record : result.selected) {
        SelectionEntry entry;
        entry.algorithm = result.name;
        entry.src = record.src;
        entry.dst = record.dst;
        entry.shape_vector = record.node_mem_range;
        entry.purify_rounds = record.purify_rounds;
        entry.fidelity = (double)record.fidelity;
        entry.success_probability = (double)record.success_probability;
        entry.expected_werner = (double)record.expected_werner;
        entries.push_back(entry);
    }
    sort(entries.begin(), entries.end(), selection_order);
    return entries;
}

string path_string_paper(const Shape_vector& shape_vector) {
    ostringstream output;
    for(size_t index = 0; index < shape_vector.size(); ++index) {
        if(index) output << '-';
        output << shape_vector[index].first + 1;
    }
    return output.str();
}

// "(1,2):0 (2,3):0 (3,4):1" - pumping rounds per physical link of the path.
string purify_by_link_string(const Shape_vector& shape_vector,
                             const vector<int>& purify_rounds) {
    ostringstream output;
    for(size_t link = 0; link + 1 < shape_vector.size(); ++link) {
        if(link) output << ' ';
        const int rounds =
            link < purify_rounds.size() ? purify_rounds[link] : 0;
        output << '(' << shape_vector[link].first + 1 << ','
               << shape_vector[link + 1].first + 1 << "):" << rounds;
    }
    return output.str();
}

string timing_string_paper(const Shape_vector& shape_vector) {
    ostringstream output;
    for(size_t node_index = 0;
        node_index < shape_vector.size(); ++node_index) {
        if(node_index) output << ';';
        output << shape_vector[node_index].first + 1 << ":[";
        const auto& ranges = shape_vector[node_index].second;
        for(size_t range_index = 0;
            range_index < ranges.size(); ++range_index) {
            if(range_index) output << '|';
            output << ranges[range_index].first << '-'
                   << ranges[range_index].second;
        }
        output << ']';
    }
    return output.str();
}

struct Numerology {
    int node_count = 0;
    int time_limit = 0;
    vector<int> theta;      // node * time_limit + time
    int total_units = 0;    // sum over (v, t): the node-time memory footprint
    int peak_units = 0;     // max over (v, t)
    int first_slot = -1;
    int last_slot = -1;

    int at(int node, int time) const {
        return theta[(size_t)node * time_limit + time];
    }
};

Numerology make_numerology(int node_count,
                           int time_limit,
                           const Shape_vector& shape_vector,
                           const vector<int>& purify_rounds) {
    Numerology result;
    result.node_count = node_count;
    result.time_limit = time_limit;
    compute_theta(node_count, time_limit, shape_vector,
                  purify_rounds, result.theta);
    for(int node = 0; node < node_count; ++node) {
        for(int time = 0; time < time_limit; ++time) {
            const int units = result.at(node, time);
            if(units == 0) continue;
            result.total_units += units;
            result.peak_units = max(result.peak_units, units);
            if(result.first_slot < 0) result.first_slot = time;
            result.first_slot = min(result.first_slot, time);
            result.last_slot = max(result.last_slot, time);
        }
    }
    return result;
}

// A numerology grid in the orientation of Fig. 2(b): one row per slot with
// time increasing upwards, one column per node.
void print_numerology_grid(ostream& output,
                           const Numerology& numerology,
                           const string& indent) {
    output << indent << setw(6) << left << "slot" << right;
    for(int node = 0; node < numerology.node_count; ++node) {
        output << setw(5) << ("v" + to_string(node + 1));
    }
    output << '\n';
    for(int time = numerology.time_limit - 1; time >= 0; --time) {
        output << indent << setw(6) << left
               << ("t" + to_string(time)) << right;
        for(int node = 0; node < numerology.node_count; ++node) {
            const int units = numerology.at(node, time);
            if(units == 0) output << setw(5) << ".";
            else output << setw(5) << units;
        }
        output << '\n';
    }
}

void write_selection_header(ofstream& output) {
    output
        << "case,algorithm,entry_index,request,src_1based,dst_1based,"
        << "src_0based,dst_0based,path_1based,hop_count,purify_rounds_total,"
        << "purify_by_link,first_slot,last_slot,span_slots,"
        << "fidelity,werner,success_probability,expected_werner,"
        << "memory_slot_units,peak_node_memory,node_time_ranges\n";
}

void write_selection_rows(ofstream& output,
                          const CaseSpec& spec,
                          const vector<SelectionEntry>& entries) {
    for(size_t index = 0; index < entries.size(); ++index) {
        const SelectionEntry& entry = entries[index];
        const Numerology numerology = make_numerology(
            spec.node_count, spec.time_limit,
            entry.shape_vector, entry.purify_rounds);
        int purify_total = 0;
        for(int rounds : entry.purify_rounds) purify_total += rounds;
        output << spec.name << ',' << entry.algorithm << ',' << index << ','
               << '(' << entry.src + 1 << ';' << entry.dst + 1 << ')' << ','
               << entry.src + 1 << ',' << entry.dst + 1 << ','
               << entry.src << ',' << entry.dst << ','
               << path_string_paper(entry.shape_vector) << ','
               << entry.shape_vector.size() - 1 << ',' << purify_total << ','
               << '"' << purify_by_link_string(
                      entry.shape_vector, entry.purify_rounds) << '"' << ','
               << numerology.first_slot << ',' << numerology.last_slot << ','
               << numerology.last_slot - numerology.first_slot + 1 << ','
               << entry.fidelity << ','
               << Purification::fidelity_to_werner(entry.fidelity) << ','
               << entry.success_probability << ','
               << entry.expected_werner << ','
               << numerology.total_units << ','
               << numerology.peak_units << ','
               << '"' << timing_string_paper(entry.shape_vector) << '"'
               << '\n';
    }
}

void write_numerology_header(ofstream& output) {
    output << "case,algorithm,scope,entry_index,request,node_1based,"
           << "time_slot,memory_units,node_capacity\n";
}

void write_numerology_rows(ofstream& output,
                           const CaseSpec& spec,
                           const string& algorithm,
                           const vector<SelectionEntry>& entries) {
    vector<int> aggregate((size_t)spec.node_count * spec.time_limit, 0);

    for(size_t index = 0; index < entries.size(); ++index) {
        const SelectionEntry& entry = entries[index];
        const Numerology numerology = make_numerology(
            spec.node_count, spec.time_limit,
            entry.shape_vector, entry.purify_rounds);
        const string request = "(" + to_string(entry.src + 1) + ";" +
                               to_string(entry.dst + 1) + ")";
        for(int node = 0; node < spec.node_count; ++node) {
            for(int time = 0; time < spec.time_limit; ++time) {
                const int units = numerology.at(node, time);
                aggregate[(size_t)node * spec.time_limit + time] += units;
                output << spec.name << ',' << algorithm << ",per_request,"
                       << index << ',' << request << ',' << node + 1 << ','
                       << time << ',' << units << ','
                       << spec.memory_per_node << '\n';
            }
        }
    }

    for(int node = 0; node < spec.node_count; ++node) {
        for(int time = 0; time < spec.time_limit; ++time) {
            output << spec.name << ',' << algorithm << ",total,-1,all,"
                   << node + 1 << ',' << time << ','
                   << aggregate[(size_t)node * spec.time_limit + time]
                   << ',' << spec.memory_per_node << '\n';
        }
    }
}

// One human-readable block per algorithm: the chosen path, its pumping
// schedule, the resulting fidelity/probability, and the numerology grid.
void write_schedule_report(ostream& output,
                           const CaseSpec& spec,
                           const string& algorithm,
                           double objective,
                           double optimum,
                           const vector<SelectionEntry>& entries) {
    const double gap = optimum > OBJECTIVE_TOLERANCE
        ? max(0.0, 100.0 * (optimum - objective) / optimum)
        : 0.0;
    output << "case=" << spec.name << " algorithm=" << algorithm
           << " expected_werner_sum=" << fixed << setprecision(6) << objective
           << " accepted=" << entries.size();
    if(algorithm == "OPT") output << " optimality_gap_pct=0.000000 (exact)";
    else output << " optimality_gap_pct=" << gap;
    output << '\n';

    Numerology aggregate;
    aggregate.node_count = spec.node_count;
    aggregate.time_limit = spec.time_limit;
    aggregate.theta.assign((size_t)spec.node_count * spec.time_limit, 0);

    for(size_t index = 0; index < entries.size(); ++index) {
        const SelectionEntry& entry = entries[index];
        const Numerology numerology = make_numerology(
            spec.node_count, spec.time_limit,
            entry.shape_vector, entry.purify_rounds);
        for(size_t cell = 0; cell < numerology.theta.size(); ++cell) {
            aggregate.theta[cell] += numerology.theta[cell];
        }
        output << "  request=(" << entry.src + 1 << ',' << entry.dst + 1
               << ") path=" << path_string_paper(entry.shape_vector)
               << " purify=[" << purify_by_link_string(
                      entry.shape_vector, entry.purify_rounds) << ']'
               << " slots=" << numerology.first_slot << ".."
               << numerology.last_slot
               << " fidelity=" << setprecision(6) << entry.fidelity
               << " werner="
               << Purification::fidelity_to_werner(entry.fidelity)
               << " prob=" << entry.success_probability
               << " prob*werner=" << entry.expected_werner
               << " mem_units=" << numerology.total_units
               << " peak_mem=" << numerology.peak_units << '\n';
        output << "    node_time_ranges="
               << timing_string_paper(entry.shape_vector) << '\n';
        output << "    numerology theta_m(t,v):\n";
        print_numerology_grid(output, numerology, "      ");
    }

    output << "  aggregate occupancy (capacity " << spec.memory_per_node
           << " per node-slot):\n";
    print_numerology_grid(output, aggregate, "      ");
    output << '\n';
}

void print_selection_table_header() {
    cout << "    " << setw(8) << left << "algo" << setw(9) << left << "request"
         << setw(10) << left << "path" << setw(24) << left << "purify/link"
         << setw(8) << left << "slots" << right
         << setw(10) << "fidelity" << setw(10) << "werner"
         << setw(10) << "prob" << setw(11) << "prob*w"
         << setw(6) << "mem" << '\n';
}

// Compact stdout table so the reviewer numbers can be read off directly.
void print_selection_table(const string& algorithm,
                           const vector<SelectionEntry>& entries,
                           int node_count,
                           int time_limit) {
    for(const SelectionEntry& entry : entries) {
        const Numerology numerology = make_numerology(
            node_count, time_limit, entry.shape_vector, entry.purify_rounds);
        ostringstream request;
        request << '(' << entry.src + 1 << ',' << entry.dst + 1 << ')';
        ostringstream slots;
        slots << numerology.first_slot << ".." << numerology.last_slot;
        cout << "    " << setw(8) << left << algorithm
             << setw(9) << left << request.str()
             << setw(10) << left << path_string_paper(entry.shape_vector)
             << setw(24) << left << purify_by_link_string(
                    entry.shape_vector, entry.purify_rounds)
             << setw(8) << left << slots.str() << right
             << setw(10) << entry.fidelity
             << setw(10) << Purification::fidelity_to_werner(entry.fidelity)
             << setw(10) << entry.success_probability
             << setw(11) << entry.expected_werner
             << setw(6) << numerology.total_units << '\n';
    }
}

// ---------------------------------------------------------------------------
// Instance disclosure: every physical and resource parameter the case ran
// with, read back out of the Graph rather than restated, so a figure or table
// built from this file cannot drift from the model.
// ---------------------------------------------------------------------------

void write_instance_header(ofstream& output) {
    output << "case,scope,key,node_a,node_b,value\n";
}

void write_instance_rows(ofstream& output,
                         const CaseSpec& spec,
                         Graph& graph) {
    auto global = [&](const string& key, double value) {
        output << spec.name << ",global," << key << ",," << ',' << value
               << '\n';
    };
    auto link = [&](int left, int right, const string& key, double value) {
        // 1-based node labels, as in the paper.
        output << spec.name << ",link," << key << ',' << left + 1 << ','
               << right + 1 << ',' << value << '\n';
    };
    auto node = [&](int index, const string& key, double value) {
        output << spec.name << ",node," << key << ',' << index + 1 << ','
               << ',' << value << '\n';
    };

    global("nodes", spec.node_count);
    global("edges", (double)spec.edges.size());
    global("requests", (double)spec.requests.size());
    global("time_limit", spec.time_limit);
    global("memory_per_node", spec.memory_per_node);
    global("fidelity_threshold", graph.get_fidelity_threshold());
    global("werner_threshold",
           Purification::fidelity_to_werner(graph.get_fidelity_threshold()));
    global("swap_probability", SWAP_PROBABILITY);
    global("slot_duration_s", graph.get_tao());
    global("memory_coherence_s", graph.get_T());
    global("decoherence_kappa", graph.get_n());
    global("decoherence_eta", graph.get_tao() / graph.get_T());
    global("gamma_per_km", graph.get_Gamma());
    global("lambda_per_km", graph.get_entangle_lambda());
    global("attempt_duration_s", graph.get_entangle_time());
    global("entangle_attempts", graph.get_entangle_attempts());
    global("min_link_fidelity", MIN_LINK_FIDELITY);
    global("max_link_fidelity", MAX_LINK_FIDELITY);
    global("max_purification_rounds", SMALL_SCALE_MAX_PURIFICATION_ROUNDS);
    global("epsilon", SMALL_SCALE_EPSILON);
    global("bucket_eps", SMALL_SCALE_BUCKET_EPS);

    for(const EdgeSpec& edge : spec.edges) {
        const double fidelity = graph.get_F_init(edge.left, edge.right);
        const double werner = Purification::fidelity_to_werner(fidelity);
        // Sec. III-A1 inverted: w_e = exp(-Gamma * l)  =>  l = -ln(w_e)/Gamma.
        const double length = graph.get_Gamma() > 0.0 && werner > 0.0
            ? -log(werner) / graph.get_Gamma()
            : 0.0;
        link(edge.left, edge.right, "fidelity_ratio", edge.fidelity_ratio);
        link(edge.left, edge.right, "fidelity", fidelity);
        link(edge.left, edge.right, "werner", werner);
        link(edge.left, edge.right, "length_km", length);
        link(edge.left, edge.right, "entangle_probability",
             graph.get_entangle_succ_prob(edge.left, edge.right));
    }

    for(int index = 0; index < spec.node_count; ++index) {
        node(index, "memory", graph.get_node_memory_at(index, 0));
        node(index, "swap_probability", graph.get_node_swap_prob(index));
        node(index, "degree", (double)graph.adj_list[index].size());
    }

    for(size_t index = 0; index < spec.requests.size(); ++index) {
        const SDpair& request = spec.requests[index];
        output << spec.name << ",request,fidelity_threshold,"
               << request.first + 1 << ',' << request.second + 1 << ','
               << graph.get_fidelity_threshold() << '\n';
    }
}

void write_csv_header(ofstream& output) {
    output
        << "case,nodes,edges,time_limit,memory_per_node,requests,"
        << "source,destination,request_pairs,min_link_fidelity,max_link_fidelity,"
        << "fidelity_threshold,epsilon,bucket_eps,max_purification_rounds,"
        << "algorithm,"
        << "proven_optimal,expected_werner_sum,optimality_gap_pct,"
        << "actual_requests,expected_requests,runtime_ms,"
        << "enumerated_schedules,feasible_schedules,"
        << "nondominated_candidates,search_states\n";
}

void write_exact_row(ofstream& output,
                     const CaseSpec& spec,
                     const ExactResult& result,
                     double runtime_ms) {
    output << spec.name << ',' << spec.node_count << ',' << spec.edges.size()
           << ',' << spec.time_limit << ',' << spec.memory_per_node << ','
           << spec.requests.size() << ',' << spec.requests.front().first << ','
           << spec.requests.front().second << ','
           << request_pairs_string(spec.requests) << ','
           << MIN_LINK_FIDELITY << ','
           << MAX_LINK_FIDELITY << ',' << FIDELITY_THRESHOLD << ','
           << SMALL_SCALE_EPSILON << ',' << SMALL_SCALE_BUCKET_EPS << ','
           << SMALL_SCALE_MAX_PURIFICATION_ROUNDS
           << ",OPT,1," << result.objective
           << ",0," << result.accepted_requests << ','
           << result.expected_requests << ',' << runtime_ms << ','
           << result.enumerated_schedules << ','
           << result.feasible_schedules << ','
           << result.nondominated_candidates << ','
           << result.search_states << '\n';
}

void write_algorithm_row(ofstream& output,
                         const CaseSpec& spec,
                         const AlgorithmResult& result,
                         double optimum) {
    const double gap = optimum > OBJECTIVE_TOLERANCE
        ? (fabs(optimum - result.objective) <= 1e-9
               ? 0.0
               : max(0.0, 100.0 * (optimum - result.objective) / optimum))
        : 0.0;
    output << spec.name << ',' << spec.node_count << ',' << spec.edges.size()
           << ',' << spec.time_limit << ',' << spec.memory_per_node << ','
           << spec.requests.size() << ',' << spec.requests.front().first << ','
           << spec.requests.front().second << ','
           << request_pairs_string(spec.requests) << ','
           << MIN_LINK_FIDELITY << ','
           << MAX_LINK_FIDELITY << ',' << FIDELITY_THRESHOLD << ','
           << SMALL_SCALE_EPSILON << ',' << SMALL_SCALE_BUCKET_EPS << ','
           << SMALL_SCALE_MAX_PURIFICATION_ROUNDS << ','
           << result.name << ",0,"
           << result.objective << ',' << gap << ','
           << result.accepted_requests << ',' << result.expected_requests
           << ',' << result.runtime_ms << ",,,,\n";
}

void write_certificate(ofstream& output,
                       const CaseSpec& spec,
                       const ExactResult& result) {
    output << "case=" << spec.name
           << " proven_optimal=1"
           << " objective=" << result.objective
           << " accepted=" << result.accepted_requests
           << " search_states=" << result.search_states << '\n';
    for(size_t index = 0; index < result.selected.size(); ++index) {
        const SDpair request = result.selected[index].first;
        const Candidate& candidate = result.selected[index].second;
        output << "  selection=" << index
               << " request=" << request.first << "->" << request.second
               << " path=" << path_string(candidate.shape_vector)
               << " purify_rounds="
               << rounds_string(candidate.purify_rounds)
               << " node_time_ranges="
               << timing_string(candidate.shape_vector)
               << " fidelity=" << candidate.fidelity
               << " success_probability=" << candidate.success_probability
               << " expected_werner=" << candidate.objective << '\n';
    }
}

} // namespace

int main() {
    try {
        const string data_directory = locate_data_directory();
        const string input_directory = data_directory + "/input";
        const string answer_directory = data_directory + "/ans";

        const string csv_path =
            answer_directory + "/main_small_scale_results.csv";
        const string certificate_path =
            answer_directory + "/main_small_scale_optimal_schedules.txt";
        const string selection_path =
            answer_directory + "/main_small_scale_selection.csv";
        const string numerology_path =
            answer_directory + "/main_small_scale_numerology.csv";
        const string report_path =
            answer_directory + "/main_small_scale_schedules.txt";
        const string instance_path =
            answer_directory + "/main_small_scale_instance.csv";
        ofstream csv(csv_path);
        ofstream certificate(certificate_path);
        ofstream selection_csv(selection_path);
        ofstream numerology_csv(numerology_path);
        ofstream report(report_path);
        ofstream instance_csv(instance_path);
        if(!csv || !certificate || !selection_csv || !numerology_csv ||
           !report || !instance_csv) {
            throw runtime_error("cannot create small-scale result files");
        }
        csv << setprecision(17);
        certificate << setprecision(17);
        selection_csv << setprecision(17);
        write_csv_header(csv);
        write_selection_header(selection_csv);
        write_numerology_header(numerology_csv);
        instance_csv << setprecision(17);
        write_instance_header(instance_csv);

        report << "Small-scale schedules selected by the exact optimum and by\n"
               << "every evaluated algorithm.  Node labels are 1-based as in\n"
               << "the paper; slots are batch slot indices 0..T-1.\n"
               << "  purify=[(u,v):r ...]  pumping rounds used on each link\n"
               << "  werner                (4F-1)/3 of the end-to-end pair\n"
               << "  prob                  end-to-end success probability\n"
               << "  prob*werner           the contribution to Eq. (12a)\n"
               << "  mem_units             sum of theta_m(t,v) over the batch\n"
               << "  theta_m(t,v)          memory cells held at node v in t\n\n";

        cout << fixed << setprecision(6);
        cout << "Small-scale exact-optimum experiment\n"
             << "objective = sum(success probability * final Werner value)\n"
             << "epsilon=" << SMALL_SCALE_EPSILON
             << ", bucket_eps=" << SMALL_SCALE_BUCKET_EPS
             << ", fidelity_threshold=" << FIDELITY_THRESHOLD << "\n\n";

        for(const CaseSpec& spec : experiment_cases()) {
            const string graph_path =
                input_directory + "/small_scale_" + spec.name + ".input";
            write_graph_file(graph_path, spec);
            Graph graph = load_graph(graph_path, spec);
            write_instance_rows(instance_csv, spec, graph);
            const map<SDpair, vector<Path>> paths =
                build_all_paths(graph, spec.requests);

            const auto exact_start = chrono::steady_clock::now();
            ExactResult optimum = solve_exact(graph, spec.requests, paths);
            const auto exact_finish = chrono::steady_clock::now();
            const double exact_runtime_ms = chrono::duration<double, milli>(
                exact_finish - exact_start).count();

            write_exact_row(csv, spec, optimum, exact_runtime_ms);
            write_certificate(certificate, spec, optimum);

            const vector<SelectionEntry> optimum_entries =
                exact_entries(optimum);
            write_selection_rows(selection_csv, spec, optimum_entries);
            write_numerology_rows(
                numerology_csv, spec, "OPT", optimum_entries);
            write_schedule_report(
                report, spec, "OPT", optimum.objective,
                optimum.objective, optimum_entries);

            cout << '[' << spec.name << "] OPT=" << optimum.objective
                 << ", accepted=" << optimum.accepted_requests
                 << ", candidates=" << optimum.nondominated_candidates
                 << ", states=" << optimum.search_states
                 << ", runtime_ms=" << exact_runtime_ms << '\n';

            const vector<AlgorithmResult> algorithm_results =
                run_algorithms(graph, spec.requests, paths);
            vector<SelectionEntry> wpfa_entries;
            for(const AlgorithmResult& result : algorithm_results) {
                if(result.objective > optimum.objective + 1e-8) {
                    throw runtime_error(
                        result.name + " exceeds exhaustive optimum in " +
                        spec.name + "; exact-model definitions are inconsistent");
                }
                write_algorithm_row(csv, spec, result, optimum.objective);
                const vector<SelectionEntry> entries =
                    algorithm_entries(result);
                write_selection_rows(selection_csv, spec, entries);
                write_numerology_rows(
                    numerology_csv, spec, result.name, entries);
                write_schedule_report(
                    report, spec, result.name, result.objective,
                    optimum.objective, entries);
                if(result.name == "ZFA2") wpfa_entries = entries;
                const double gap =
                    fabs(optimum.objective - result.objective) <= 1e-9
                    ? 0.0
                    : (optimum.objective > OBJECTIVE_TOLERANCE
                           ? 100.0 *
                             (optimum.objective - result.objective) /
                             optimum.objective
                           : 0.0);
                cout << "  " << setw(8) << left << result.name << right
                     << " objective=" << result.objective
                     << ", gap=" << gap << "%"
                     << ", runtime_ms=" << result.runtime_ms << '\n';
            }

            // Side-by-side view of what OPT and WPFA actually scheduled.
            cout << "  selected schedules (1-based nodes, batch slots):\n";
            print_selection_table_header();
            print_selection_table(
                "OPT", optimum_entries, spec.node_count, spec.time_limit);
            print_selection_table(
                "WPFA", wpfa_entries, spec.node_count, spec.time_limit);
            cout << '\n';
        }

        cout << "CSV:          " << csv_path << '\n';
        cout << "Certificate:  " << certificate_path << '\n';
        cout << "Selections:   " << selection_path << '\n';
        cout << "Numerologies: " << numerology_path << '\n';
        cout << "Schedules:    " << report_path << '\n';
        cout << "Instance:     " << instance_path << '\n';
        return 0;
    } catch(const exception& error) {
        cerr << "main_small_scale: " << error.what() << '\n';
        return 1;
    }
}
