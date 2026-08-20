#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <utility>
#include <vector>

#include "Algorithm/AlgorithmBase/AlgorithmBase.h"
#include "Algorithm/EFiRAP/EFiRAP.h"
#include "Algorithm/EFiRAP_longtime/EFiRAP_longtime.h"
#include "Algorithm/WernerAlgo2/WernerAlgo2.h"

// WernerAlgo2.h currently leaks this macro. Keep it from changing the
// benchmark's standard-library and timing types.
#ifdef double
#undef double
#endif

#include "ExperimentWorkload.h"
#include "Network/Graph/Graph.h"
#include "Network/PathMethod/Greedy/Greedy.h"

using namespace std;

namespace {

struct Config {
    string input_pattern = "../data/input/round_{}.input";
    string request_file;
    string output_file = "../data/ans/runtime_compare.csv";
    vector<int> time_limits{5, 7, 9, 11, 13, 15, 17, 19};
    vector<string> algorithms{"WPFA", "EFiRAP", "EFiRAP-time"};
    int instances = 5;
    int repetitions = 1;
    int warmups = 0;
    int request_count = 80;
    int k_paths = 5;
    uint32_t request_seed = 20260820U;
    string python_command = "python3";
    bool regenerate_inputs = true;
    bool quiet = false;
};

struct WorkloadInstance {
    string input_file;
    vector<SDpair> requests;
};

struct Sample {
    int time_limit = 0;
    int instance = 0;
    int repetition = 0;
    string algorithm;
    double seconds = 0.0;
    double actual_requests = 0.0;
    double expected_requests = 0.0;
};

class NullBuffer : public streambuf {
protected:
    int overflow(int character) override {
        return traits_type::not_eof(character);
    }
};

vector<string> split(const string& value, char delimiter) {
    vector<string> fields;
    string field;
    istringstream input(value);
    while(getline(input, field, delimiter)) {
        if(!field.empty()) fields.push_back(field);
    }
    return fields;
}

vector<int> parse_integer_list(const string& value, const string& option) {
    vector<int> result;
    for(const string& field : split(value, ',')) {
        try {
            size_t consumed = 0;
            int parsed = stoi(field, &consumed);
            if(consumed != field.size()) throw invalid_argument("trailing text");
            result.push_back(parsed);
        } catch(const exception&) {
            throw invalid_argument(option + " expects comma-separated integers: " + value);
        }
    }
    if(result.empty()) throw invalid_argument(option + " cannot be empty");
    return result;
}

string canonical_algorithm_name(string name) {
    transform(name.begin(), name.end(), name.begin(),
              [](unsigned char character) { return (char)tolower(character); });
    if(name == "wpfa" || name == "zfa2") return "WPFA";
    if(name == "efirap") return "EFiRAP";
    if(name == "efirap-time" || name == "efirap_time" ||
       name == "efirap-longtime" || name == "efirap_longtime") {
        return "EFiRAP-time";
    }
    throw invalid_argument("unknown algorithm: " + name);
}

void print_usage(const char* executable) {
    cout
        << "Usage: " << executable << " [options]\n\n"
        << "Measures only AlgorithmBase::run(), matching main.cpp's runtime metric.\n"
        << "By default it regenerates main.cpp's 100-node graphs and uses the\n"
        << "same T=13 stratified 200-request pool (then takes its first N).\n"
        << "Input paths use {} as the zero-based instance placeholder.\n\n"
        << "Options:\n"
        << "  --input-pattern PATH   Default: ../data/input/round_{}.input\n"
        << "  --request-file PATH    Override generated requests with a fixed pair list\n"
        << "  --output PATH          Raw CSV output (summary uses *_summary.csv)\n"
        << "  --time-limits LIST     Comma-separated horizons (default: 5,7,...,19)\n"
        << "  --algorithms LIST      WPFA,EFiRAP,EFiRAP-time\n"
        << "  --instances N          Number of graph input files (default: 5)\n"
        << "  --repetitions N        Timed runs per graph/horizon (default: 1)\n"
        << "  --warmups N            Untimed runs before samples (default: 0)\n"
        << "  --requests N           Prefix length from main's 200-request pool\n"
        << "  --seed N               Base graph/request seed (default: 20260820)\n"
        << "  --k-paths N            EFiRAP Yen path count (default: 5)\n"
        << "  --python COMMAND       Python command for graph_generator.py\n"
        << "  --reuse-inputs         Do not regenerate round_*.input files\n"
        << "  --quiet                Suppress algorithm stderr while benchmarking\n"
        << "  --help                 Show this message\n";
}

int parse_positive(const string& value, const string& option, bool allow_zero = false) {
    size_t consumed = 0;
    int parsed;
    try {
        parsed = stoi(value, &consumed);
    } catch(const exception&) {
        throw invalid_argument(option + " expects an integer: " + value);
    }
    if(consumed != value.size() || parsed < (allow_zero ? 0 : 1)) {
        throw invalid_argument(option + " has an invalid value: " + value);
    }
    return parsed;
}

Config parse_arguments(int argc, char** argv) {
    Config config;
    auto require_value = [&](int& index, const string& option) -> string {
        if(index + 1 >= argc) throw invalid_argument(option + " requires a value");
        return argv[++index];
    };

    for(int index = 1; index < argc; ++index) {
        string option = argv[index];
        if(option == "--help") {
            print_usage(argv[0]);
            exit(0);
        } else if(option == "--input-pattern") {
            config.input_pattern = require_value(index, option);
        } else if(option == "--request-file") {
            config.request_file = require_value(index, option);
        } else if(option == "--output") {
            config.output_file = require_value(index, option);
        } else if(option == "--time-limits") {
            config.time_limits = parse_integer_list(require_value(index, option), option);
        } else if(option == "--algorithms") {
            config.algorithms.clear();
            for(const string& name : split(require_value(index, option), ',')) {
                string canonical = canonical_algorithm_name(name);
                if(find(config.algorithms.begin(), config.algorithms.end(), canonical) ==
                   config.algorithms.end()) {
                    config.algorithms.push_back(canonical);
                }
            }
            if(config.algorithms.empty()) throw invalid_argument(option + " cannot be empty");
        } else if(option == "--instances") {
            config.instances = parse_positive(require_value(index, option), option);
        } else if(option == "--repetitions") {
            config.repetitions = parse_positive(require_value(index, option), option);
        } else if(option == "--warmups") {
            config.warmups = parse_positive(require_value(index, option), option, true);
        } else if(option == "--requests") {
            config.request_count = parse_positive(require_value(index, option), option);
        } else if(option == "--seed") {
            string value = require_value(index, option);
            size_t consumed = 0;
            unsigned long parsed;
            try {
                parsed = stoul(value, &consumed);
            } catch(const exception&) {
                throw invalid_argument(option + " expects a non-negative integer: " + value);
            }
            if(consumed != value.size()) {
                throw invalid_argument(option + " has an invalid value: " + value);
            }
            config.request_seed = (uint32_t)parsed;
        } else if(option == "--k-paths") {
            config.k_paths = parse_positive(require_value(index, option), option);
        } else if(option == "--python") {
            config.python_command = require_value(index, option);
        } else if(option == "--reuse-inputs") {
            config.regenerate_inputs = false;
        } else if(option == "--quiet") {
            config.quiet = true;
        } else {
            throw invalid_argument("unknown option: " + option);
        }
    }

    for(int time_limit : config.time_limits) {
        if(time_limit < 2) throw invalid_argument("every time limit must be at least 2");
    }
    if(config.request_file.empty() && config.request_count > 200) {
        throw invalid_argument(
            "--requests cannot exceed main.cpp's 200-request pool");
    }
    return config;
}

string input_path_for(const Config& config, int instance) {
    string path = config.input_pattern;
    size_t placeholder = path.find("{}");
    if(placeholder == string::npos) {
        if(config.instances != 1) {
            throw invalid_argument(
                "--input-pattern must contain {} when --instances is greater than 1");
        }
        return path;
    }
    path.replace(placeholder, 2, to_string(instance));
    return path;
}

vector<SDpair> read_requests(const string& filename, int node_count) {
    ifstream input(filename);
    if(!input) throw runtime_error("cannot open request file: " + filename);

    vector<SDpair> requests;
    string line;
    int line_number = 0;
    while(getline(input, line)) {
        ++line_number;
        size_t comment = line.find('#');
        if(comment != string::npos) line.erase(comment);
        istringstream row(line);
        int source, destination;
        if(!(row >> source)) continue;
        if(!(row >> destination)) {
            throw runtime_error("invalid request at " + filename + ":" +
                                to_string(line_number));
        }
        string trailing;
        if(row >> trailing || source < 0 || destination < 0 ||
           source >= node_count || destination >= node_count ||
           source == destination) {
            throw runtime_error("invalid request at " + filename + ":" +
                                to_string(line_number));
        }
        requests.push_back({source, destination});
    }
    if(requests.empty()) throw runtime_error("request file is empty: " + filename);
    return requests;
}

map<SDpair, vector<Path>> build_paths(const Graph& graph,
                                      const vector<SDpair>& requests) {
    Graph path_graph = graph;
    path_graph.increase_resources(10);
    Greedy path_method;
    path_method.build_paths(path_graph, requests);

    map<SDpair, set<Path>> unique_paths;
    for(const auto& entry : path_method.get_paths()) {
        for(const Path& path : entry.second) unique_paths[entry.first].insert(path);
    }

    // Match main.cpp: make every explicit two-hop alternative available to
    // WPFA, rather than depending only on Greedy's capacity-driven choices.
    for(const SDpair& request : requests) {
        int source = request.first;
        int destination = request.second;
        for(int intermediate : graph.adj_list[source]) {
            if(graph.adj_set[intermediate].count(destination)) {
                unique_paths[request].insert({source, intermediate, destination});
            }
        }
    }

    map<SDpair, vector<Path>> paths;
    for(const auto& entry : unique_paths) {
        paths[entry.first] = vector<Path>(entry.second.begin(), entry.second.end());
    }
    return paths;
}

unique_ptr<AlgorithmBase> make_algorithm(
    const string& name,
    const Graph& graph,
    const vector<SDpair>& requests,
    const map<SDpair, vector<Path>>& paths,
    int k_paths) {
    if(name == "WPFA") {
        return unique_ptr<AlgorithmBase>(new WernerAlgo2(graph, requests, paths));
    }
    if(name == "EFiRAP") {
        return unique_ptr<AlgorithmBase>(
            new EFiRAP(graph, requests, paths, k_paths));
    }
    if(name == "EFiRAP-time") {
        return unique_ptr<AlgorithmBase>(
            new EFiRAP_longtime(graph, requests, paths, k_paths));
    }
    throw invalid_argument("unknown algorithm: " + name);
}

bool algorithm_available(const string& name) {
    if(name == "WPFA") return true;
    if(name == "EFiRAP") return EFiRAP::gurobi_available();
    if(name == "EFiRAP-time") return EFiRAP_longtime::gurobi_available();
    return false;
}

Sample run_sample(const string& name,
                  const Graph& graph,
                  const vector<SDpair>& requests,
                  const map<SDpair, vector<Path>>& paths,
                  const Config& config,
                  int time_limit,
                  int instance,
                  int repetition) {
    unique_ptr<AlgorithmBase> algorithm =
        make_algorithm(name, graph, requests, paths, config.k_paths);

    NullBuffer null_buffer;
    streambuf* original_buffer = nullptr;
    if(config.quiet) original_buffer = cerr.rdbuf(&null_buffer);

    const auto start = chrono::steady_clock::now();
    try {
        algorithm->run();
    } catch(...) {
        if(original_buffer != nullptr) cerr.rdbuf(original_buffer);
        throw;
    }
    const auto finish = chrono::steady_clock::now();
    if(original_buffer != nullptr) cerr.rdbuf(original_buffer);

    Sample sample;
    sample.time_limit = time_limit;
    sample.instance = instance;
    sample.repetition = repetition;
    sample.algorithm = name;
    sample.seconds = chrono::duration<double>(finish - start).count();
    sample.actual_requests = algorithm->get_res("actual_req_cnt");
    sample.expected_requests = algorithm->get_res("succ_request_cnt");
    return sample;
}

string summary_filename(const string& raw_filename) {
    size_t dot = raw_filename.find_last_of('.');
    size_t separator = raw_filename.find_last_of("/\\");
    if(dot == string::npos || (separator != string::npos && dot < separator)) {
        return raw_filename + "_summary.csv";
    }
    return raw_filename.substr(0, dot) + "_summary" + raw_filename.substr(dot);
}

double median(vector<double> values) {
    sort(values.begin(), values.end());
    size_t middle = values.size() / 2;
    if(values.size() % 2 == 1) return values[middle];
    return (values[middle - 1] + values[middle]) / 2.0;
}

void write_summary(const string& filename, const vector<Sample>& samples) {
    map<pair<int, string>, vector<const Sample*>> groups;
    for(const Sample& sample : samples) {
        groups[{sample.time_limit, sample.algorithm}].push_back(&sample);
    }

    ofstream output(filename, ios::trunc);
    if(!output) throw runtime_error("cannot open summary output: " + filename);
    output << "time_limit,algorithm,samples,mean_seconds,median_seconds,"
              "stddev_seconds,min_seconds,max_seconds,mean_actual_requests,"
              "mean_expected_requests\n";
    output << fixed << setprecision(9);

    for(const auto& entry : groups) {
        vector<double> runtimes;
        double actual_sum = 0.0;
        double expected_sum = 0.0;
        for(const Sample* sample : entry.second) {
            runtimes.push_back(sample->seconds);
            actual_sum += sample->actual_requests;
            expected_sum += sample->expected_requests;
        }
        double mean = accumulate(runtimes.begin(), runtimes.end(), 0.0) /
                      runtimes.size();
        double squared_error = 0.0;
        for(double runtime : runtimes) {
            double error = runtime - mean;
            squared_error += error * error;
        }
        double standard_deviation = sqrt(squared_error / runtimes.size());
        auto minimum_and_maximum = minmax_element(runtimes.begin(), runtimes.end());

        output << entry.first.first << ',' << entry.first.second << ','
               << runtimes.size() << ',' << mean << ',' << median(runtimes) << ','
               << standard_deviation << ',' << *minimum_and_maximum.first << ','
               << *minimum_and_maximum.second << ','
               << actual_sum / runtimes.size() << ','
               << expected_sum / runtimes.size() << '\n';
    }
}

Graph load_graph(const string& input_file, int time_limit) {
    return Graph(
        input_file,
        time_limit,
        0.9,             // swapping probability
        10,              // average memory
        0.80,            // minimum initial fidelity
        0.99,            // maximum initial fidelity
        0.80,            // end-to-end fidelity threshold
        0.25, 0.75, 2.0, // decoherence A, B, n
        0.04, 0.002,     // decoherence T, delta (tao)
        0.02702867239,   // Zmin
        0.001,           // shared bucket epsilon (WPFA overrides to 0.0001)
        0.001,           // time eta
        0.01,            // delta P
        0.045,           // entangling lambda
        0.00025);        // entangling attempt time
}

void prepare_graph_input(const Config& config,
                         const string& input_file,
                         int instance) {
    if(!config.regenerate_inputs) {
        ifstream existing(input_file);
        if(!existing) {
            throw runtime_error("missing graph input: " + input_file);
        }
        return;
    }

    const uint32_t seed = config.request_seed + (uint32_t)instance;
    const string command = config.python_command
        + " graph_generator.py \"" + input_file + "\" 100 "
        + to_string(seed);
    cerr << "[runtime_compare] " << command << '\n';
    if(system(command.c_str()) != 0) {
        throw runtime_error("graph_generator.py failed for " + input_file);
    }
}

vector<WorkloadInstance> prepare_workloads(const Config& config) {
    vector<WorkloadInstance> workloads;
    workloads.reserve(config.instances);

    for(int instance = 0; instance < config.instances; ++instance) {
        WorkloadInstance workload;
        workload.input_file = input_path_for(config, instance);
        prepare_graph_input(config, workload.input_file, instance);

        // main.cpp creates its shared request pool from the default T=13
        // graph, independently of the time-limit sweep.
        Graph request_graph = load_graph(workload.input_file, 13);
        if(config.request_file.empty()) {
            RequestGenerationConfig request_config;
            request_config.minimum_hops = 2;
            request_config.maximum_hops = 4;
            request_config.hotspot_node_limit = 2;
            request_config.hotspot_candidate_limit = 8;
            request_config.minimum_repetitions = 1;
            request_config.maximum_repetitions = 3;
            request_config.random_seed =
                config.request_seed + (uint32_t)instance;
            workload.requests = generate_stratified_requests(
                request_graph, 200, request_config);
        } else {
            workload.requests = read_requests(
                config.request_file, request_graph.get_num_nodes());
        }

        if((int)workload.requests.size() < config.request_count) {
            throw runtime_error(
                "request workload has fewer pairs than --requests");
        }
        workload.requests.resize(config.request_count);
        workloads.push_back(move(workload));
    }
    return workloads;
}

} // namespace

int main(int argc, char** argv) {
    try {
        Config config = parse_arguments(argc, argv);

        vector<string> available_algorithms;
        for(const string& name : config.algorithms) {
            if(algorithm_available(name)) {
                available_algorithms.push_back(name);
            } else {
                cerr << "[runtime_compare] skipping " << name
                     << ": this binary was built without Gurobi support\n";
            }
        }
        if(available_algorithms.empty()) {
            throw runtime_error("none of the requested algorithms is available");
        }

        // Graph/request generation is deliberately outside every timed region.
        const vector<WorkloadInstance> workloads = prepare_workloads(config);

        ofstream raw_output(config.output_file, ios::trunc);
        if(!raw_output) {
            throw runtime_error("cannot open raw output: " + config.output_file);
        }
        raw_output << "time_limit,instance,repetition,algorithm,run_seconds,"
                      "actual_requests,expected_requests\n";
        raw_output << fixed << setprecision(9);

        vector<Sample> samples;
        for(int time_limit : config.time_limits) {
            for(int instance = 0; instance < config.instances; ++instance) {
                const WorkloadInstance& workload = workloads[instance];
                Graph graph = load_graph(workload.input_file, time_limit);
                const vector<SDpair>& requests = workload.requests;
                map<SDpair, vector<Path>> paths = build_paths(graph, requests);

                for(const string& name : available_algorithms) {
                    for(int warmup = 0; warmup < config.warmups; ++warmup) {
                        (void)run_sample(name, graph, requests, paths, config,
                                         time_limit, instance, -1 - warmup);
                    }
                    for(int repetition = 0;
                        repetition < config.repetitions;
                        ++repetition) {
                        Sample sample = run_sample(
                            name, graph, requests, paths, config,
                            time_limit, instance, repetition);
                        samples.push_back(sample);
                        raw_output << sample.time_limit << ',' << sample.instance << ','
                                   << sample.repetition << ',' << sample.algorithm << ','
                                   << sample.seconds << ',' << sample.actual_requests << ','
                                   << sample.expected_requests << '\n';
                        raw_output.flush();
                        cout << "[runtime_compare] T=" << time_limit
                             << " instance=" << instance
                             << " repetition=" << repetition
                             << " algorithm=" << name
                             << " seconds=" << fixed << setprecision(6)
                             << sample.seconds << '\n';
                    }
                }
            }
        }

        raw_output.close();
        const string summary = summary_filename(config.output_file);
        write_summary(summary, samples);
        cout << "[runtime_compare] raw samples: " << config.output_file << '\n'
             << "[runtime_compare] summary: " << summary << '\n';
        return 0;
    } catch(const exception& error) {
        cerr << "[runtime_compare] error: " << error.what() << '\n';
        return 1;
    }
}
