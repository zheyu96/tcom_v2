// Small-scale parameter sweeps comparing the exhaustive OPT solver with WPFA.
// Both methods receive the same candidate path set and deterministic
// three/four-node physical instances.
//
// Build and run from src/:
//   make main_smallscale
//   ./main_smallscale
//   ./main_smallscale --sweep request_cnt

#define SMALL_SCALE_EXPERIMENT_LIBRARY_ONLY
#include "main_small_scale.cpp"

#include "Network/PathMethod/Greedy/Greedy.h"

#include <memory>
#include <set>

using namespace std;

namespace {

constexpr int DEFAULT_REQUEST_COUNT = 4;
constexpr int DEFAULT_MEMORY = 4;
constexpr double DEFAULT_FIDELITY_THRESHOLD = 0.80;
constexpr double DEFAULT_SLOT_DURATION = 0.002;
constexpr double DEFAULT_SWAP_PROBABILITY = 0.90;
constexpr double MAIN_STYLE_EPSILON = 0.55;
constexpr double MAIN_STYLE_GRAPH_BUCKET_EPS = 0.01;
constexpr double MAIN_STYLE_WPFA_BUCKET_EPS = 0.001;

const vector<string> SWEEP_NAMES = {
    "request_cnt",
    "fidelity_threshold",
    "tao",
    "swap_prob",
    "avg_memory",
};

const vector<string> METRIC_NAMES = {
    "fidelity_gain",
    "succ_request_cnt",
    "actual_req_cnt",
    "runtime",
};

struct Options {
    string selected_sweep;
};

struct TopologySpec {
    string name;
    int node_count;
    int time_limit;
    vector<EdgeSpec> edges;
    vector<SDpair> request_pool;
};

struct TrialSpec {
    string name;
    string sweep;
    double parameter_value = 0.0;
    string topology;
    int node_count = 0;
    int time_limit = 0;
    int memory_per_node = DEFAULT_MEMORY;
    double fidelity_threshold = DEFAULT_FIDELITY_THRESHOLD;
    double slot_duration = DEFAULT_SLOT_DURATION;
    double swap_probability = DEFAULT_SWAP_PROBABILITY;
    vector<EdgeSpec> edges;
    vector<SDpair> requests;
};

struct TrialResult {
    TrialSpec spec;
    map<SDpair, vector<Path>> paths;
    bool has_exact = false;
    ExactResult optimum;
    double exact_runtime_ms = 0.0;
    vector<AlgorithmResult> algorithms;
};

struct Aggregate {
    int samples = 0;
    double fidelity_gain = 0.0;
    double expected_requests = 0.0;
    double accepted_requests = 0.0;
    double runtime_seconds = 0.0;
};

void print_help(const char* executable) {
    cout << "Usage: " << executable << " [options]\n"
         << "  --sweep NAME  run only request_cnt, fidelity_threshold, tao, "
            "swap_prob, or avg_memory\n"
         << "  --help        show this message\n";
}

bool is_known_sweep(const string& name) {
    return find(SWEEP_NAMES.begin(), SWEEP_NAMES.end(), name) !=
           SWEEP_NAMES.end();
}

Options parse_options(int argc, char** argv) {
    Options options;
    for(int index = 1; index < argc; ++index) {
        const string option = argv[index];
        if(option == "--help") {
            print_help(argv[0]);
            exit(0);
        }
        if(option == "--sweep") {
            if(index + 1 >= argc) {
                throw invalid_argument("missing value after --sweep");
            }
            options.selected_sweep = argv[++index];
            if(!is_known_sweep(options.selected_sweep)) {
                throw invalid_argument(
                    "unknown sweep: " + options.selected_sweep);
            }
            continue;
        }
        throw invalid_argument("unknown option: " + option);
    }
    return options;
}

const vector<TopologySpec>& topologies() {
    // Ratios are mapped by Graph into [0.80, 0.99]. They remain fixed for all
    // sweeps, and the configured link-fidelity lower bound stays at 0.80.
    static const vector<TopologySpec> values = {
        {
            "line3", 3, 5,
            {{0, 1, 0.90}, {1, 2, 0.82}},
            {{0, 2}, {0, 2}, {0, 1}, {1, 2}, {0, 2}, {1, 2}},
        },
        {
            "diamond4", 4, 5,
            {{0, 1, 0.86}, {1, 3, 0.95},
             {0, 2, 0.95}, {2, 3, 0.80}},
            {{0, 3}, {0, 3}, {1, 2}, {0, 2}, {1, 3}, {0, 3}},
        },
    };
    return values;
}

const vector<double>& sweep_values(const string& sweep) {
    static const map<string, vector<double>> values = {
        {"request_cnt", {2, 3, 4, 5, 6}},
        {"fidelity_threshold", {0.76, 0.78, 0.80, 0.82, 0.84}},
        {"tao", {0.0010, 0.0015, 0.0020, 0.0025, 0.0030}},
        {"swap_prob", {0.70, 0.75, 0.80, 0.85, 0.90}},
        {"avg_memory", {2, 3, 4, 5, 6}},
    };
    return values.at(sweep);
}

string value_token(double value) {
    ostringstream output;
    output << fixed << setprecision(6) << value;
    string token = output.str();
    while(token.size() > 1 && token.back() == '0') token.pop_back();
    if(!token.empty() && token.back() == '.') token.pop_back();
    replace(token.begin(), token.end(), '.', 'p');
    return token;
}

TrialSpec make_trial(const TopologySpec& topology,
                     const string& sweep,
                     double value) {
    TrialSpec trial;
    trial.sweep = sweep;
    trial.parameter_value = value;
    trial.topology = topology.name;
    trial.node_count = topology.node_count;
    trial.time_limit = topology.time_limit;
    trial.edges = topology.edges;

    int request_count = DEFAULT_REQUEST_COUNT;
    if(sweep == "request_cnt") {
        request_count = (int)llround(value);
    } else if(sweep == "fidelity_threshold") {
        trial.fidelity_threshold = value;
    } else if(sweep == "tao") {
        trial.slot_duration = value;
    } else if(sweep == "swap_prob") {
        trial.swap_probability = value;
    } else if(sweep == "avg_memory") {
        trial.memory_per_node = (int)llround(value);
    } else {
        throw invalid_argument("unknown sweep: " + sweep);
    }

    if(request_count < 1 ||
       request_count > (int)topology.request_pool.size()) {
        throw logic_error("request count is outside the small-scale pool");
    }
    trial.requests.assign(topology.request_pool.begin(),
                          topology.request_pool.begin() + request_count);
    trial.name = sweep + "_" + value_token(value) + "_" + topology.name;
    return trial;
}

void write_trial_graph(const string& filename, const TrialSpec& spec) {
    ofstream output(filename);
    if(!output) throw runtime_error("cannot write graph: " + filename);
    output << spec.node_count << '\n';
    // Graph adds memory_per_node to each zero stored in the input file.
    for(int node = 0; node < spec.node_count; ++node) output << "0\n";
    output << spec.edges.size() << '\n' << setprecision(17);
    for(const EdgeSpec& edge : spec.edges) {
        output << edge.left << ' ' << edge.right << ' '
               << edge.fidelity_ratio << '\n';
    }
}

Graph load_trial_graph(const string& filename, const TrialSpec& spec) {
    return Graph(
        filename, spec.time_limit, spec.swap_probability,
        spec.memory_per_node, MIN_LINK_FIDELITY, MAX_LINK_FIDELITY,
        spec.fidelity_threshold, DECOHERENCE_A, DECOHERENCE_B,
        DECOHERENCE_N, DECOHERENCE_T, spec.slot_duration, Z_MIN,
        MAIN_STYLE_GRAPH_BUCKET_EPS, TIME_ETA, DELTA_P,
        ENTANGLE_LAMBDA, ENTANGLE_TIME);
}

map<SDpair, vector<Path>> build_shared_paths(
    const Graph& graph,
    const vector<SDpair>& requests) {
    // Mirror main.cpp: resource-expanded shortest paths plus all explicit
    // two-hop alternatives. Every algorithm receives this same path set.
    Graph path_graph = graph;
    path_graph.increase_resources(10);
    Greedy method;
    method.build_paths(path_graph, requests);

    map<SDpair, set<Path>> unique_paths;
    for(const auto& entry : method.get_paths()) {
        for(const Path& path : entry.second) {
            unique_paths[entry.first].insert(path);
        }
    }
    for(const SDpair& request : requests) {
        for(int intermediate : graph.adj_list[request.first]) {
            if(graph.adj_set[intermediate].count(request.second)) {
                unique_paths[request].insert(
                    {request.first, intermediate, request.second});
            }
        }
    }

    map<SDpair, vector<Path>> paths;
    for(const SDpair& request : requests) {
        const set<Path>& candidates = unique_paths[request];
        if(candidates.empty()) {
            throw runtime_error(
                "no candidate path for " + to_string(request.first) +
                "->" + to_string(request.second));
        }
        paths[request] = vector<Path>(candidates.begin(), candidates.end());
    }
    return paths;
}

AlgorithmResult run_named_algorithm(
    const string& display_name,
    unique_ptr<AlgorithmBase> algorithm) {
    AlgorithmResult result = run_algorithm(std::move(algorithm));
    result.name = display_name;
    return result;
}

vector<string> algorithm_names() {
    return {"OPT", "WPFA"};
}

vector<AlgorithmResult> run_wpfa(
    const Graph& graph,
    const vector<SDpair>& requests,
    const map<SDpair, vector<Path>>& paths) {
    vector<AlgorithmResult> results;
    unique_ptr<WernerAlgo2> algorithm(new WernerAlgo2(
        graph, requests, paths,
        MAIN_STYLE_EPSILON, MAIN_STYLE_WPFA_BUCKET_EPS));
    algorithm->set_detailed_logging(false);
    results.push_back(run_named_algorithm("WPFA", std::move(algorithm)));
    return results;
}

TrialResult run_trial(const TrialSpec& spec,
                      const string& input_directory) {
    const string graph_path =
        input_directory + "/main_smallscale_" + spec.name + ".input";
    write_trial_graph(graph_path, spec);
    Graph graph = load_trial_graph(graph_path, spec);

    TrialResult result;
    result.spec = spec;
    result.paths = build_shared_paths(graph, spec.requests);

    const auto start = chrono::steady_clock::now();
    result.optimum = solve_exact(graph, spec.requests, result.paths);
    const auto finish = chrono::steady_clock::now();
    result.exact_runtime_ms = chrono::duration<double, milli>(
        finish - start).count();
    result.has_exact = true;

    result.algorithms = run_wpfa(graph, spec.requests, result.paths);
    for(const AlgorithmResult& algorithm : result.algorithms) {
        if(algorithm.objective > result.optimum.objective + 1e-8) {
            throw runtime_error(
                algorithm.name + " exceeds exhaustive OPT in " + spec.name);
        }
    }
    return result;
}

void write_results_header(ofstream& output) {
    output
        << "instance,sweep,parameter_value,topology,nodes,edges,requests,"
        << "time_limit,memory_per_node,min_link_fidelity,max_link_fidelity,"
        << "fidelity_threshold,tao,swap_probability,epsilon,"
        << "graph_bucket_eps,wpfa_bucket_eps,"
        << "algorithm,display_order,proven_optimal,fidelity_gain,"
        << "optimality_gap_pct,actual_requests,expected_requests,runtime_ms,"
        << "candidate_paths,enumerated_schedules,feasible_schedules,"
        << "nondominated_candidates,search_states\n";
}

int candidate_path_count(const TrialResult& result) {
    int count = 0;
    for(const auto& entry : result.paths) count += entry.second.size();
    return count;
}

void write_trial_rows(ofstream& output, const TrialResult& result) {
    const TrialSpec& spec = result.spec;
    auto write_prefix = [&]() {
        output << spec.name << ',' << spec.sweep << ','
               << spec.parameter_value << ',' << spec.topology << ','
               << spec.node_count << ',' << spec.edges.size() << ','
               << spec.requests.size() << ',' << spec.time_limit << ','
               << spec.memory_per_node << ',' << MIN_LINK_FIDELITY << ','
               << MAX_LINK_FIDELITY << ',' << spec.fidelity_threshold << ','
               << spec.slot_duration << ',' << spec.swap_probability << ','
               << MAIN_STYLE_EPSILON << ',' << MAIN_STYLE_GRAPH_BUCKET_EPS
               << ','
               << MAIN_STYLE_WPFA_BUCKET_EPS << ',';
    };

    if(result.has_exact) {
        write_prefix();
        output << "OPT,0,1," << result.optimum.objective << ",0,"
               << result.optimum.accepted_requests << ','
               << result.optimum.expected_requests << ','
               << result.exact_runtime_ms << ','
               << candidate_path_count(result) << ','
               << result.optimum.enumerated_schedules << ','
               << result.optimum.feasible_schedules << ','
               << result.optimum.nondominated_candidates << ','
               << result.optimum.search_states << '\n';
    }

    for(size_t index = 0; index < result.algorithms.size(); ++index) {
        const AlgorithmResult& algorithm = result.algorithms[index];
        write_prefix();
        output << algorithm.name << ',' << (index + 1) << ",0,"
               << algorithm.objective << ',';
        if(result.has_exact &&
           result.optimum.objective > OBJECTIVE_TOLERANCE) {
            output << max(
                0.0,
                100.0 * (result.optimum.objective - algorithm.objective) /
                    result.optimum.objective);
        }
        output << ',' << algorithm.accepted_requests << ','
               << algorithm.expected_requests << ',' << algorithm.runtime_ms
               << ',' << candidate_path_count(result) << ",,,,\n";
    }
}

using AggregateTable =
    map<string, map<double, map<string, Aggregate>>>;

AggregateTable aggregate_results(const vector<TrialResult>& results) {
    AggregateTable table;
    for(const TrialResult& trial : results) {
        Aggregate& optimum = table[trial.spec.sweep]
                                  [trial.spec.parameter_value]
                                  ["OPT"];
        ++optimum.samples;
        optimum.fidelity_gain += trial.optimum.objective;
        optimum.expected_requests += trial.optimum.expected_requests;
        optimum.accepted_requests += trial.optimum.accepted_requests;
        optimum.runtime_seconds += trial.exact_runtime_ms / 1000.0;

        for(const AlgorithmResult& algorithm : trial.algorithms) {
            Aggregate& aggregate = table[trial.spec.sweep]
                                        [trial.spec.parameter_value]
                                        [algorithm.name];
            ++aggregate.samples;
            aggregate.fidelity_gain += algorithm.objective;
            aggregate.expected_requests += algorithm.expected_requests;
            aggregate.accepted_requests += algorithm.accepted_requests;
            aggregate.runtime_seconds += algorithm.runtime_ms / 1000.0;
        }
    }
    return table;
}

double aggregate_metric(const Aggregate& aggregate, const string& metric) {
    if(aggregate.samples == 0) {
        return numeric_limits<double>::quiet_NaN();
    }
    if(metric == "fidelity_gain") {
        return aggregate.fidelity_gain / aggregate.samples;
    }
    if(metric == "succ_request_cnt") {
        return aggregate.expected_requests / aggregate.samples;
    }
    if(metric == "actual_req_cnt") {
        return aggregate.accepted_requests / aggregate.samples;
    }
    if(metric == "runtime") {
        return aggregate.runtime_seconds / aggregate.samples;
    }
    throw invalid_argument("unknown metric: " + metric);
}

void write_summary(const string& filename,
                   const AggregateTable& table,
                   const vector<string>& names) {
    ofstream output(filename);
    if(!output) throw runtime_error("cannot write summary: " + filename);
    output << setprecision(17)
           << "sweep,parameter_value,algorithm,display_order,samples,"
           << "mean_fidelity_gain,mean_expected_requests,"
           << "mean_actual_requests,mean_runtime_seconds\n";
    for(const string& sweep : SWEEP_NAMES) {
        const auto sweep_it = table.find(sweep);
        if(sweep_it == table.end()) continue;
        for(double value : sweep_values(sweep)) {
            for(size_t index = 0; index < names.size(); ++index) {
                const Aggregate& aggregate =
                    sweep_it->second.at(value).at(names[index]);
                output << sweep << ',' << value << ',' << names[index]
                       << ',' << index << ',' << aggregate.samples << ','
                       << aggregate_metric(aggregate, "fidelity_gain") << ','
                       << aggregate_metric(aggregate, "succ_request_cnt")
                       << ','
                       << aggregate_metric(aggregate, "actual_req_cnt") << ','
                       << aggregate_metric(aggregate, "runtime") << '\n';
            }
        }
    }
}

void write_ans_files(const string& answer_directory,
                     const AggregateTable& table,
                     const vector<string>& names) {
    for(const string& sweep : SWEEP_NAMES) {
        const auto sweep_it = table.find(sweep);
        if(sweep_it == table.end()) continue;
        for(const string& metric : METRIC_NAMES) {
            const string filename = answer_directory + "/SmallScale_" +
                                    sweep + "_" + metric + ".ans";
            ofstream output(filename);
            if(!output) throw runtime_error("cannot write ANS: " + filename);
            output << setprecision(17);
            for(double value : sweep_values(sweep)) {
                output << value;
                for(const string& name : names) {
                    const Aggregate& aggregate =
                        sweep_it->second.at(value).at(name);
                    output << ' ' << aggregate_metric(aggregate, metric);
                }
                output << '\n';
            }
        }
    }
}

void print_trial(const TrialResult& result) {
    cout << '[' << result.spec.sweep << '=' << result.spec.parameter_value
         << ", " << result.spec.topology << ']';
    if(result.has_exact) cout << " OPT=" << result.optimum.objective;
    for(const AlgorithmResult& algorithm : result.algorithms) {
        cout << ' ' << algorithm.name << '=' << algorithm.objective;
    }
    cout << '\n';
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        const string data_directory = locate_data_directory();
        const string input_directory = data_directory + "/input";
        const string answer_directory = data_directory + "/ans";
        const string results_path =
            answer_directory + "/main_smallscale_results.csv";
        const string summary_path =
            answer_directory + "/main_smallscale_summary.csv";

        ofstream results_output(results_path);
        if(!results_output) {
            throw runtime_error("cannot write results: " + results_path);
        }
        results_output << setprecision(17);
        write_results_header(results_output);

        vector<string> selected_sweeps = SWEEP_NAMES;
        if(!options.selected_sweep.empty()) {
            selected_sweeps = {options.selected_sweep};
        }
        const vector<string> names = algorithm_names();

        cout << fixed << setprecision(6)
             << "Small-scale 3/4-node parameter sweeps\n"
             << "minimum link fidelity=" << MIN_LINK_FIDELITY
             << ", default fidelity threshold="
             << DEFAULT_FIDELITY_THRESHOLD
             << ", exact OPT=enabled\n"
             << "algorithm order=";
        for(size_t index = 0; index < names.size(); ++index) {
            if(index) cout << ',';
            cout << names[index];
        }
        cout << '\n';
        vector<TrialResult> results;
        for(const string& sweep : selected_sweeps) {
            for(double value : sweep_values(sweep)) {
                for(const TopologySpec& topology : topologies()) {
                    TrialResult result = run_trial(
                        make_trial(topology, sweep, value),
                        input_directory);
                    write_trial_rows(results_output, result);
                    print_trial(result);
                    results.push_back(std::move(result));
                }
            }
        }
        results_output.close();

        const AggregateTable aggregates = aggregate_results(results);
        write_summary(summary_path, aggregates, names);
        write_ans_files(answer_directory, aggregates, names);

        cout << "Results: " << results_path << '\n'
             << "Summary: " << summary_path << '\n'
             << "Chart data: " << answer_directory
             << "/SmallScale_<sweep>_<metric>.ans\n";
        return 0;
    } catch(const exception& error) {
        cerr << "main_smallscale: " << error.what() << '\n';
        return 1;
    }
}
