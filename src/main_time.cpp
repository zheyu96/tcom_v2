#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "Algorithm/AlgorithmBase/AlgorithmBase.h"
#include "Algorithm/EFiRAP/EFiRAP.h"
#include "Algorithm/EFiRAP_longtime/EFiRAP_longtime.h"
#include "Algorithm/MyAlgo1/MyAlgo1.h"
#include "Algorithm/MyAlgo3/MyAlgo3.h"
#include "Algorithm/WernerAlgo/WernerAlgo.h"
#include "Algorithm/WernerAlgo2/WernerAlgo2.h"
#include "Algorithm/WernerAlgo3/WernerAlgo3.h"

// WernerAlgo2.h currently leaks this macro. Do not let it alter the
// benchmark's standard-library types or chrono::duration<double>.
#ifdef double
#undef double
#endif

#include "ExperimentWorkload.h"
#include "Network/Graph/Graph.h"
#include "Network/PathMethod/Greedy/Greedy.h"

using namespace std;

namespace {

const string REQUEST_COUNT = "request_cnt";
const string FIDELITY_THRESHOLD = "fidelity_threshold";
const string TIME_LIMIT = "time_limit";

struct Config {
    string input_pattern = "../data/input/main_time_round_{}.input";
    string output_directory = "../data/ans";
    string python_command = "python3";
    vector<string> sweeps{REQUEST_COUNT, FIDELITY_THRESHOLD, TIME_LIMIT};
    vector<double> request_counts{80, 100, 120, 140, 160};
    vector<double> fidelity_thresholds{
        0.50, 0.55, 0.60, 0.65, 0.70, 0.75, 0.80, 0.85, 0.90, 0.95};
    vector<double> time_limits{5, 7, 9, 11, 13, 15, 17, 19};
    vector<string> algorithms{
        "ZFA_UB", "ZFA", "ZFA2", "MyAlgo1", "MyAlgo3",
        "EFiRAP", "EFiRAP_longtime"};
    int instances = 5;
    int repetitions = 1;
    int warmups = 0;
    uint32_t seed = 20260820U;
    bool regenerate_inputs = true;
    bool show_algorithm_output = false;
};

struct ExperimentSettings {
    int request_count = 100;
    int time_limit = 13;
    double fidelity_threshold = 0.80;
};

struct Workload {
    string input_file;
    vector<SDpair> request_pool;
};

struct Sample {
    string sweep;
    double parameter_value = 0.0;
    int instance = 0;
    int repetition = 0;
    string algorithm;
    double seconds = 0.0;
};

class NullBuffer : public streambuf {
protected:
    int overflow(int character) override {
        return traits_type::not_eof(character);
    }
};

class ScopedQuietStreams {
public:
    explicit ScopedQuietStreams(bool quiet) : quiet_(quiet) {
        if(quiet_) {
            cout_buffer_ = cout.rdbuf(&null_buffer_);
            cerr_buffer_ = cerr.rdbuf(&null_buffer_);
        }
    }

    ~ScopedQuietStreams() {
        if(quiet_) {
            cout.rdbuf(cout_buffer_);
            cerr.rdbuf(cerr_buffer_);
        }
    }

    ScopedQuietStreams(const ScopedQuietStreams&) = delete;
    ScopedQuietStreams& operator=(const ScopedQuietStreams&) = delete;

private:
    bool quiet_;
    NullBuffer null_buffer_;
    streambuf* cout_buffer_ = nullptr;
    streambuf* cerr_buffer_ = nullptr;
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

vector<double> parse_number_list(const string& value, const string& option) {
    vector<double> result;
    for(const string& field : split(value, ',')) {
        try {
            size_t consumed = 0;
            double parsed = stod(field, &consumed);
            if(consumed != field.size() || !isfinite(parsed)) {
                throw invalid_argument("invalid value");
            }
            result.push_back(parsed);
        } catch(const exception&) {
            throw invalid_argument(
                option + " expects comma-separated numbers: " + value);
        }
    }
    if(result.empty()) throw invalid_argument(option + " cannot be empty");
    return result;
}

int parse_nonnegative_integer(
    const string& value,
    const string& option,
    bool allow_zero) {
    try {
        size_t consumed = 0;
        int parsed = stoi(value, &consumed);
        if(consumed != value.size() || parsed < (allow_zero ? 0 : 1)) {
            throw invalid_argument("invalid value");
        }
        return parsed;
    } catch(const exception&) {
        throw invalid_argument(option + " expects " +
            (allow_zero ? "a non-negative" : "a positive") + " integer");
    }
}

string lowercase(string value) {
    transform(value.begin(), value.end(), value.begin(),
              [](unsigned char character) { return (char)tolower(character); });
    return value;
}

string canonical_sweep_name(const string& value) {
    string name = lowercase(value);
    if(name == "request" || name == "requests" || name == "request_count" ||
       name == REQUEST_COUNT) {
        return REQUEST_COUNT;
    }
    if(name == "fidelity" || name == "threshold" ||
       name == FIDELITY_THRESHOLD) {
        return FIDELITY_THRESHOLD;
    }
    if(name == "time" || name == "t" || name == TIME_LIMIT) {
        return TIME_LIMIT;
    }
    throw invalid_argument("unknown sweep: " + value);
}

string canonical_algorithm_name(const string& value) {
    string name = lowercase(value);
    if(name == "zfa_ub" || name == "zfa-ub" || name == "ub") {
        return "ZFA_UB";
    }
    if(name == "zfa" || name == "wpfa-nopurify" ||
       name == "wpfa-no-purify") {
        return "ZFA";
    }
    if(name == "zfa2" || name == "wpfa") return "ZFA2";
    if(name == "myalgo1" || name == "fnpr") return "MyAlgo1";
    if(name == "myalgo3" || name == "flto") return "MyAlgo3";
    if(name == "efirap") return "EFiRAP";
    if(name == "efirap_longtime" || name == "efirap-longtime" ||
       name == "efirap-long") {
        return "EFiRAP_longtime";
    }
    throw invalid_argument("unknown algorithm: " + value);
}

void append_unique(vector<string>& values, const string& value) {
    if(find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

void print_usage(const char* executable) {
    cout
        << "Usage: " << executable << " [options]\n\n"
        << "Measures run() runtime while independently sweeping request_cnt,\n"
        << "fidelity_threshold, and time_limit. Graph generation, request\n"
        << "generation, path finding, constructors, and result I/O are outside\n"
        << "the timed region.\n\n"
        << "Options:\n"
        << "  --sweeps LIST              request_cnt,fidelity_threshold,time_limit\n"
        << "  --request-counts LIST      Default: 80,100,120,140,160\n"
        << "  --fidelity-thresholds LIST Default: 0.50,0.55,...,0.95\n"
        << "  --time-limits LIST         Default: 5,7,9,11,13,15,17,19\n"
        << "  --algorithms LIST          ZFA_UB,ZFA,ZFA2,MyAlgo1,MyAlgo3,\n"
        << "                             EFiRAP,EFiRAP_longtime\n"
        << "  --instances N              Seeded graph instances (default: 5)\n"
        << "  --repetitions N            Timed repetitions per instance (default: 1)\n"
        << "  --warmups N                Untimed runs per instance (default: 0)\n"
        << "  --seed N                   Base graph/request seed (default: 20260820)\n"
        << "  --input-pattern PATH       {} is replaced by the instance index\n"
        << "  --output-dir PATH          Existing output directory (default: ../data/ans)\n"
        << "  --python COMMAND           Graph generator command (default: python3)\n"
        << "  --reuse-inputs             Use existing input files\n"
        << "  --show-algorithm-output    Do not suppress algorithm stdout/stderr\n"
        << "  --help                     Show this message\n\n"
        << "Outputs:\n"
        << "  main_time_runtime_raw.csv, main_time_runtime_summary.csv, and\n"
        << "  Greedy_<sweep>_runtime.ans (compatible with ChartGenerator.py).\n";
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
        } else if(option == "--sweeps") {
            config.sweeps.clear();
            for(const string& name : split(require_value(index, option), ',')) {
                append_unique(config.sweeps, canonical_sweep_name(name));
            }
        } else if(option == "--request-counts") {
            config.request_counts = parse_number_list(require_value(index, option), option);
        } else if(option == "--fidelity-thresholds") {
            config.fidelity_thresholds =
                parse_number_list(require_value(index, option), option);
        } else if(option == "--time-limits") {
            config.time_limits = parse_number_list(require_value(index, option), option);
        } else if(option == "--algorithms") {
            config.algorithms.clear();
            for(const string& name : split(require_value(index, option), ',')) {
                append_unique(config.algorithms, canonical_algorithm_name(name));
            }
        } else if(option == "--instances") {
            config.instances = parse_nonnegative_integer(
                require_value(index, option), option, false);
        } else if(option == "--repetitions") {
            config.repetitions = parse_nonnegative_integer(
                require_value(index, option), option, false);
        } else if(option == "--warmups") {
            config.warmups = parse_nonnegative_integer(
                require_value(index, option), option, true);
        } else if(option == "--seed") {
            string value = require_value(index, option);
            try {
                size_t consumed = 0;
                unsigned long parsed = stoul(value, &consumed);
                if(consumed != value.size() || parsed > UINT32_MAX) {
                    throw invalid_argument("invalid value");
                }
                config.seed = (uint32_t)parsed;
            } catch(const exception&) {
                throw invalid_argument(option + " expects a 32-bit unsigned integer");
            }
        } else if(option == "--input-pattern") {
            config.input_pattern = require_value(index, option);
        } else if(option == "--output-dir") {
            config.output_directory = require_value(index, option);
        } else if(option == "--python") {
            config.python_command = require_value(index, option);
        } else if(option == "--reuse-inputs") {
            config.regenerate_inputs = false;
        } else if(option == "--show-algorithm-output") {
            config.show_algorithm_output = true;
        } else {
            throw invalid_argument("unknown option: " + option);
        }
    }

    if(config.sweeps.empty()) throw invalid_argument("--sweeps cannot be empty");
    if(config.algorithms.empty()) throw invalid_argument("--algorithms cannot be empty");
    for(double value : config.request_counts) {
        if(value < 1 || value != floor(value)) {
            throw invalid_argument("request counts must be positive integers");
        }
    }
    for(double value : config.fidelity_thresholds) {
        if(value <= 0.25 || value > 1.0) {
            throw invalid_argument("fidelity thresholds must be in (0.25, 1]");
        }
    }
    for(double value : config.time_limits) {
        if(value < 2 || value != floor(value)) {
            throw invalid_argument("time limits must be integers of at least 2");
        }
    }
    return config;
}

string join_path(const string& directory, const string& filename) {
    if(directory.empty()) return filename;
    char last = directory.back();
    if(last == '/' || last == '\\') return directory + filename;
    return directory + "/" + filename;
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

Graph load_graph(
    const string& input_file,
    int time_limit,
    double fidelity_threshold) {
    return Graph(
        input_file,
        time_limit,
        0.9,             // swapping probability
        10,              // average memory
        0.80,            // minimum initial fidelity
        0.99,            // maximum initial fidelity
        fidelity_threshold,
        0.25, 0.75, 2.0, // decoherence A, B, n
        0.04, 0.002,     // decoherence T, tao
        0.02702867239,   // Zmin
        0.01,            // graph bucket epsilon; ZFA2 supplies 0.0001
        0.001,           // time eta
        0.01,            // delta P
        0.045,           // entangling lambda
        0.00025);        // entangling attempt time
}

void prepare_graph_input(
    const Config& config,
    const string& input_file,
    int instance) {
    if(!config.regenerate_inputs) {
        ifstream existing(input_file);
        if(!existing) throw runtime_error("missing graph input: " + input_file);
        return;
    }

    const uint32_t seed = config.seed + (uint32_t)instance;
    const string command = config.python_command + " graph_generator.py \"" +
        input_file + "\" 100 " + to_string(seed) + " 1";
    cerr << "[main_time] " << command << '\n';
    if(system(command.c_str()) != 0) {
        throw runtime_error("graph_generator.py failed for " + input_file);
    }
}

int maximum_request_count(const Config& config) {
    int maximum = ExperimentSettings{}.request_count;
    if(find(config.sweeps.begin(), config.sweeps.end(), REQUEST_COUNT) !=
       config.sweeps.end()) {
        maximum = max(maximum, (int)*max_element(
            config.request_counts.begin(), config.request_counts.end()));
    }
    // Keep main.cpp's 200-request workload convention for the standard sweep.
    return max(maximum, 200);
}

vector<Workload> prepare_workloads(const Config& config) {
    vector<Workload> workloads;
    workloads.reserve(config.instances);
    const int pool_size = maximum_request_count(config);

    for(int instance = 0; instance < config.instances; ++instance) {
        Workload workload;
        workload.input_file = input_path_for(config, instance);
        prepare_graph_input(config, workload.input_file, instance);

        Graph request_graph = load_graph(workload.input_file, 13, 0.80);
        RequestGenerationConfig request_config;
        request_config.minimum_hops = 2;
        request_config.maximum_hops = 4;
        request_config.hotspot_node_limit = 2;
        request_config.hotspot_candidate_limit = 8;
        request_config.minimum_repetitions = 1;
        request_config.maximum_repetitions = 3;
        request_config.random_seed = config.seed + (uint32_t)instance;
        workload.request_pool = generate_stratified_requests(
            request_graph, pool_size, request_config);
        if((int)workload.request_pool.size() < pool_size) {
            throw runtime_error("request generator returned too few requests");
        }
        workloads.push_back(move(workload));
    }
    return workloads;
}

map<SDpair, vector<Path>> build_paths(
    const Graph& graph,
    const vector<SDpair>& requests) {
    Graph path_graph = graph;
    path_graph.increase_resources(10);
    Greedy path_method;
    path_method.build_paths(path_graph, requests);

    map<SDpair, set<Path>> unique_paths;
    for(const auto& entry : path_method.get_paths()) {
        for(const Path& path : entry.second) {
            unique_paths[entry.first].insert(path);
        }
    }

    // Match main.cpp: add every explicit two-hop alternative for every
    // algorithm, independent of Greedy's capacity-driven repetitions.
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
        paths[entry.first] = vector<Path>(
            entry.second.begin(), entry.second.end());
    }
    return paths;
}

bool algorithm_available(const string& name) {
    if(name == "EFiRAP") return EFiRAP::gurobi_available();
    if(name == "EFiRAP_longtime") return EFiRAP_longtime::gurobi_available();
    return true;
}

unique_ptr<AlgorithmBase> make_algorithm(
    const string& name,
    const Graph& graph,
    const vector<SDpair>& requests,
    const map<SDpair, vector<Path>>& paths,
    const string& experiment_label) {
    if(name == "ZFA_UB") {
        return unique_ptr<AlgorithmBase>(new WernerAlgo3(graph, requests, paths));
    }
    if(name == "ZFA") {
        return unique_ptr<AlgorithmBase>(new WernerAlgo(graph, requests, paths));
    }
    if(name == "ZFA2") {
        unique_ptr<WernerAlgo2> algorithm(
            new WernerAlgo2(graph, requests, paths));
        algorithm->set_detailed_logging(false);
        algorithm->set_experiment_label(experiment_label);
        return unique_ptr<AlgorithmBase>(algorithm.release());
    }
    if(name == "MyAlgo1") {
        return unique_ptr<AlgorithmBase>(new MyAlgo1(graph, requests, paths));
    }
    if(name == "MyAlgo3") {
        return unique_ptr<AlgorithmBase>(new MyAlgo3(graph, requests, paths));
    }
    if(name == "EFiRAP") {
        return unique_ptr<AlgorithmBase>(new EFiRAP(graph, requests, paths));
    }
    if(name == "EFiRAP_longtime") {
        return unique_ptr<AlgorithmBase>(
            new EFiRAP_longtime(graph, requests, paths));
    }
    throw invalid_argument("unknown algorithm: " + name);
}

Sample run_sample(
    const string& algorithm_name,
    const Graph& graph,
    const vector<SDpair>& requests,
    const map<SDpair, vector<Path>>& paths,
    const Config& config,
    const string& sweep,
    double parameter_value,
    int instance,
    int repetition) {
    const string label = sweep + "=" + to_string(parameter_value) +
        " instance=" + to_string(instance) +
        " repetition=" + to_string(repetition);
    unique_ptr<AlgorithmBase> algorithm = make_algorithm(
        algorithm_name, graph, requests, paths, label);

    chrono::steady_clock::time_point start;
    chrono::steady_clock::time_point finish;
    {
        ScopedQuietStreams quiet(!config.show_algorithm_output);
        start = chrono::steady_clock::now();
        algorithm->run();
        finish = chrono::steady_clock::now();
    }

    Sample sample;
    sample.sweep = sweep;
    sample.parameter_value = parameter_value;
    sample.instance = instance;
    sample.repetition = repetition;
    sample.algorithm = algorithm_name;
    sample.seconds = chrono::duration<double>(finish - start).count();
    return sample;
}

vector<double> values_for_sweep(const Config& config, const string& sweep) {
    if(sweep == REQUEST_COUNT) return config.request_counts;
    if(sweep == FIDELITY_THRESHOLD) return config.fidelity_thresholds;
    if(sweep == TIME_LIMIT) return config.time_limits;
    throw invalid_argument("unknown sweep: " + sweep);
}

ExperimentSettings settings_for(const string& sweep, double value) {
    ExperimentSettings settings;
    if(sweep == REQUEST_COUNT) settings.request_count = (int)value;
    else if(sweep == FIDELITY_THRESHOLD) settings.fidelity_threshold = value;
    else if(sweep == TIME_LIMIT) settings.time_limit = (int)value;
    else throw invalid_argument("unknown sweep: " + sweep);
    return settings;
}

double median(vector<double> values) {
    sort(values.begin(), values.end());
    size_t middle = values.size() / 2;
    if(values.size() % 2 == 1) return values[middle];
    return (values[middle - 1] + values[middle]) / 2.0;
}

vector<double> runtimes_for(
    const vector<Sample>& samples,
    const string& sweep,
    double parameter_value,
    const string& algorithm) {
    vector<double> result;
    for(const Sample& sample : samples) {
        if(sample.sweep == sweep && sample.parameter_value == parameter_value &&
           sample.algorithm == algorithm) {
            result.push_back(sample.seconds);
        }
    }
    return result;
}

void write_summary(const Config& config, const vector<Sample>& samples) {
    const string filename = join_path(
        config.output_directory, "main_time_runtime_summary.csv");
    ofstream output(filename, ios::trunc);
    if(!output) throw runtime_error("cannot open summary output: " + filename);
    output << "sweep,parameter_value,algorithm,samples,mean_seconds,"
              "median_seconds,stddev_seconds,min_seconds,max_seconds\n";
    output << fixed << setprecision(9);

    for(const string& sweep : config.sweeps) {
        for(double value : values_for_sweep(config, sweep)) {
            for(const string& algorithm : config.algorithms) {
                if(!algorithm_available(algorithm)) continue;
                vector<double> runtimes = runtimes_for(
                    samples, sweep, value, algorithm);
                if(runtimes.empty()) continue;
                double mean = accumulate(
                    runtimes.begin(), runtimes.end(), 0.0) / runtimes.size();
                double squared_error = 0.0;
                for(double runtime : runtimes) {
                    double error = runtime - mean;
                    squared_error += error * error;
                }
                double standard_deviation = sqrt(
                    squared_error / runtimes.size());
                auto bounds = minmax_element(runtimes.begin(), runtimes.end());
                output << sweep << ',' << value << ',' << algorithm << ','
                       << runtimes.size() << ',' << mean << ','
                       << median(runtimes) << ',' << standard_deviation << ','
                       << *bounds.first << ',' << *bounds.second << '\n';
            }
        }
    }
}

void write_chart_files(const Config& config, const vector<Sample>& samples) {
    for(const string& sweep : config.sweeps) {
        const string filename = join_path(
            config.output_directory, "Greedy_" + sweep + "_runtime.ans");
        ofstream output(filename, ios::trunc);
        if(!output) throw runtime_error("cannot open chart output: " + filename);
        output << setprecision(12);
        for(double value : values_for_sweep(config, sweep)) {
            output << value;
            for(const string& algorithm : config.algorithms) {
                if(!algorithm_available(algorithm)) continue;
                vector<double> runtimes = runtimes_for(
                    samples, sweep, value, algorithm);
                if(runtimes.empty()) {
                    throw runtime_error(
                        "missing runtime samples for " + sweep + "/" +
                        algorithm);
                }
                double mean = accumulate(
                    runtimes.begin(), runtimes.end(), 0.0) / runtimes.size();
                output << ' ' << mean;
            }
            output << '\n';
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        Config config = parse_arguments(argc, argv);
        vector<string> available_algorithms;
        for(const string& algorithm : config.algorithms) {
            if(algorithm_available(algorithm)) {
                available_algorithms.push_back(algorithm);
            } else {
                cerr << "[main_time] skipping " << algorithm
                     << ": this binary was built without Gurobi support\n";
            }
        }
        config.algorithms = move(available_algorithms);
        if(config.algorithms.empty()) {
            throw runtime_error("none of the requested algorithms is available");
        }

        // All workload preparation is deliberately outside timed regions.
        const vector<Workload> workloads = prepare_workloads(config);
        const string raw_filename = join_path(
            config.output_directory, "main_time_runtime_raw.csv");
        ofstream raw_output(raw_filename, ios::trunc);
        if(!raw_output) throw runtime_error("cannot open raw output: " + raw_filename);
        raw_output << "sweep,parameter_value,request_count,fidelity_threshold,"
                      "time_limit,instance,repetition,algorithm,run_seconds\n";
        raw_output << fixed << setprecision(9);

        cout << "[main_time] algorithm columns:";
        for(const string& algorithm : config.algorithms) cout << ' ' << algorithm;
        cout << '\n';

        vector<Sample> samples;
        for(const string& sweep : config.sweeps) {
            for(double value : values_for_sweep(config, sweep)) {
                const ExperimentSettings settings = settings_for(sweep, value);
                for(int instance = 0; instance < config.instances; ++instance) {
                    const Workload& workload = workloads[instance];
                    Graph graph = load_graph(
                        workload.input_file,
                        settings.time_limit,
                        settings.fidelity_threshold);
                    vector<SDpair> requests(
                        workload.request_pool.begin(),
                        workload.request_pool.begin() + settings.request_count);
                    const map<SDpair, vector<Path>> paths =
                        build_paths(graph, requests);

                    for(const string& algorithm : config.algorithms) {
                        for(int warmup = 0; warmup < config.warmups; ++warmup) {
                            (void)run_sample(
                                algorithm, graph, requests, paths, config,
                                sweep, value, instance, -1 - warmup);
                        }
                        for(int repetition = 0;
                            repetition < config.repetitions;
                            ++repetition) {
                            Sample sample = run_sample(
                                algorithm, graph, requests, paths, config,
                                sweep, value, instance, repetition);
                            samples.push_back(sample);
                            raw_output << sample.sweep << ','
                                       << sample.parameter_value << ','
                                       << settings.request_count << ','
                                       << settings.fidelity_threshold << ','
                                       << settings.time_limit << ','
                                       << sample.instance << ','
                                       << sample.repetition << ','
                                       << sample.algorithm << ','
                                       << sample.seconds << '\n';
                            raw_output.flush();
                            cout << "[main_time] " << sweep << '=' << value
                                 << " instance=" << instance
                                 << " repetition=" << repetition
                                 << " algorithm=" << algorithm
                                 << " seconds=" << fixed << setprecision(6)
                                 << sample.seconds << '\n';
                        }
                    }
                }
            }
        }

        raw_output.close();
        write_summary(config, samples);
        write_chart_files(config, samples);
        cout << "[main_time] raw samples: " << raw_filename << '\n'
             << "[main_time] summary: "
             << join_path(config.output_directory,
                          "main_time_runtime_summary.csv")
             << '\n';
        for(const string& sweep : config.sweeps) {
            cout << "[main_time] chart data: "
                 << join_path(config.output_directory,
                              "Greedy_" + sweep + "_runtime.ans")
                 << '\n';
        }
        return 0;
    } catch(const exception& error) {
        cerr << "[main_time] error: " << error.what() << '\n';
        return 1;
    }
}
