// Reviewer-facing small-scale optimality-gap experiment.
//
// This driver intentionally compares only two methods on the same discrete-
// time model:
//   (1) OPT: exhaustive enumeration of every path schedule, pumping choice,
//       operation timing, and admissible subset, followed by exact packing
//       under node-time memory constraints; and
//   (2) WPFA: WernerAlgo2 with the paper's (W,P) discretization and trimming.
//
// The detailed instance is the four-node line used in the response to R1.3.
// A deterministic random batch then varies link lengths, a common fidelity
// threshold, per-node capacities, and the five/six-slot horizon.  Every draw
// is retained: instances are never selected according to their observed gap.
//
// Build and run from src/:
//   make main_smallscale
//   ./main_smallscale
//   ./main_smallscale --instances 30 --seed 20260906

// The exact oracle and its reporting helpers live in main_small_scale.cpp.
// Suppressing that file's standalone entry point lets both historical and new
// experiment drivers use exactly the same audited oracle implementation.
#define SMALL_SCALE_EXPERIMENT_LIBRARY_ONLY
#include "main_small_scale.cpp"

#include <random>

using namespace std;

namespace {

constexpr uint32_t DEFAULT_RANDOM_SEED = 20260906u;
constexpr int DEFAULT_RANDOM_INSTANCES = 30;
constexpr double MATCH_TOLERANCE = 1e-9;
constexpr double LINK_GAMMA = 0.0044;

struct Options {
    int random_instances = DEFAULT_RANDOM_INSTANCES;
    uint32_t seed = DEFAULT_RANDOM_SEED;
    bool detailed_only = false;
};

struct TrialSpec {
    string name;
    string kind;
    int time_limit = 6;
    vector<int> capacities;
    vector<double> lengths_km;
    double fidelity_threshold = 0.80;
};

struct Comparison {
    TrialSpec spec;
    ExactResult optimum;
    AlgorithmResult wpfa;
    double exact_runtime_ms = 0.0;
    double gap_pct = 0.0;
    int optimum_purified_requests = 0;
    int wpfa_purified_requests = 0;
};

void print_help(const char* executable) {
    cout << "Usage: " << executable << " [options]\n"
         << "  --instances N   random four-node instances (default: 30)\n"
         << "  --seed N        deterministic random seed (default: 20260906)\n"
         << "  --detailed-only run only the canonical reviewer instance\n"
         << "  --help          show this message\n";
}

int parse_nonnegative_int(const string& text, const string& option) {
    size_t consumed = 0;
    const long long value = stoll(text, &consumed);
    if(consumed != text.size() || value < 0 || value > 1000000) {
        throw invalid_argument("invalid value for " + option + ": " + text);
    }
    return (int)value;
}

uint32_t parse_seed(const string& text) {
    size_t consumed = 0;
    const unsigned long long value = stoull(text, &consumed);
    if(consumed != text.size() ||
       value > numeric_limits<uint32_t>::max()) {
        throw invalid_argument("invalid value for --seed: " + text);
    }
    return (uint32_t)value;
}

Options parse_options(int argc, char** argv) {
    Options options;
    for(int index = 1; index < argc; ++index) {
        const string option = argv[index];
        if(option == "--help") {
            print_help(argv[0]);
            exit(0);
        } else if(option == "--detailed-only") {
            options.detailed_only = true;
        } else if(option == "--instances" || option == "--seed") {
            if(index + 1 >= argc) {
                throw invalid_argument("missing value after " + option);
            }
            const string value = argv[++index];
            if(option == "--instances") {
                options.random_instances =
                    parse_nonnegative_int(value, option);
            } else {
                options.seed = parse_seed(value);
            }
        } else {
            throw invalid_argument("unknown option: " + option);
        }
    }
    if(options.detailed_only) options.random_instances = 0;
    return options;
}

const vector<SDpair>& reviewer_requests() {
    static const vector<SDpair> requests = {
        {0, 2}, // R1=(1,3)
        {0, 3}, // R2=(1,4)
        {1, 3}, // R3=(2,4)
    };
    return requests;
}

double fidelity_ratio_from_length(double length_km) {
    // Graph reads a normalized fidelity ratio and reconstructs F_e.  Invert
    // that input convention after applying the paper's distance model.
    const double werner = exp(-LINK_GAMMA * length_km);
    const double fidelity = Purification::werner_to_fidelity(werner);
    const double ratio =
        (fidelity - MIN_LINK_FIDELITY) /
        (MAX_LINK_FIDELITY - MIN_LINK_FIDELITY);
    if(ratio < 0.0 || ratio > 1.0) {
        throw runtime_error(
            "sampled link length falls outside Graph's fidelity range");
    }
    return ratio;
}

double length_from_fidelity_ratio(double ratio) {
    const double fidelity =
        ratio * (MAX_LINK_FIDELITY - MIN_LINK_FIDELITY) +
        MIN_LINK_FIDELITY;
    return -log(Purification::fidelity_to_werner(fidelity)) / LINK_GAMMA;
}

string join_ints(const vector<int>& values) {
    ostringstream output;
    for(size_t index = 0; index < values.size(); ++index) {
        if(index) output << ';';
        output << values[index];
    }
    return output.str();
}

string join_doubles(const vector<double>& values) {
    ostringstream output;
    output << setprecision(10);
    for(size_t index = 0; index < values.size(); ++index) {
        if(index) output << ';';
        output << values[index];
    }
    return output.str();
}

void write_trial_graph(const string& filename, const TrialSpec& spec) {
    if(spec.capacities.size() != 4 || spec.lengths_km.size() != 3) {
        throw logic_error("a small-scale line trial must have 4 capacities "
                          "and 3 link lengths");
    }
    ofstream output(filename);
    if(!output) throw runtime_error("cannot write graph file: " + filename);
    output << "4\n";
    for(int capacity : spec.capacities) output << capacity << '\n';
    output << "3\n" << setprecision(17);
    for(int link = 0; link < 3; ++link) {
        output << link << ' ' << link + 1 << ' '
               << fidelity_ratio_from_length(spec.lengths_km[link]) << '\n';
    }
}

Graph load_trial_graph(const string& filename, const TrialSpec& spec) {
    // Node capacities are stored directly in the graph file, hence avg=0.
    return Graph(
        filename, spec.time_limit, SWAP_PROBABILITY, 0,
        MIN_LINK_FIDELITY, MAX_LINK_FIDELITY, spec.fidelity_threshold,
        DECOHERENCE_A, DECOHERENCE_B, DECOHERENCE_N, DECOHERENCE_T,
        SLOT_DURATION, Z_MIN, SMALL_SCALE_BUCKET_EPS, TIME_ETA, DELTA_P,
        ENTANGLE_LAMBDA, ENTANGLE_TIME, LINK_GAMMA);
}

int count_purified(const vector<pair<SDpair, Candidate>>& selected) {
    int count = 0;
    for(const auto& item : selected) {
        if(any_of(item.second.purify_rounds.begin(),
                  item.second.purify_rounds.end(),
                  [](int rounds) { return rounds > 0; })) {
            ++count;
        }
    }
    return count;
}

int count_purified(const vector<AcceptedShapeRecord>& selected) {
    int count = 0;
    for(const AcceptedShapeRecord& item : selected) {
        if(any_of(item.purify_rounds.begin(), item.purify_rounds.end(),
                  [](int rounds) { return rounds > 0; })) {
            ++count;
        }
    }
    return count;
}

AlgorithmResult run_wpfa(const Graph& graph,
                         const vector<SDpair>& requests,
                         const map<SDpair, vector<Path>>& paths) {
    unique_ptr<WernerAlgo2> algorithm(new WernerAlgo2(
        graph, requests, paths,
        SMALL_SCALE_EPSILON, SMALL_SCALE_BUCKET_EPS));
    algorithm->set_detailed_logging(false);
    AlgorithmResult result = run_algorithm(std::move(algorithm));
    result.name = "WPFA";
    return result;
}

Comparison compare_trial(const TrialSpec& spec,
                         const string& input_directory) {
    const string graph_path =
        input_directory + "/main_smallscale_" + spec.name + ".input";
    write_trial_graph(graph_path, spec);
    Graph graph = load_trial_graph(graph_path, spec);
    const vector<SDpair>& requests = reviewer_requests();
    const map<SDpair, vector<Path>> paths = build_all_paths(graph, requests);

    Comparison result;
    result.spec = spec;
    const auto exact_start = chrono::steady_clock::now();
    result.optimum = solve_exact(graph, requests, paths);
    const auto exact_finish = chrono::steady_clock::now();
    result.exact_runtime_ms = chrono::duration<double, milli>(
        exact_finish - exact_start).count();
    result.wpfa = run_wpfa(graph, requests, paths);

    if(result.wpfa.objective > result.optimum.objective + 1e-8) {
        throw runtime_error(
            "WPFA exceeds exhaustive OPT in " + spec.name +
            "; the two model definitions are inconsistent");
    }
    if(result.optimum.objective > OBJECTIVE_TOLERANCE) {
        result.gap_pct = max(
            0.0, 100.0 *
            (result.optimum.objective - result.wpfa.objective) /
            result.optimum.objective);
    }
    result.optimum_purified_requests =
        count_purified(result.optimum.selected);
    result.wpfa_purified_requests = count_purified(result.wpfa.selected);
    return result;
}

TrialSpec canonical_trial() {
    // Keep the already-audited R1.3 instance exactly reproducible.  Lengths
    // below are derived from its three stored fidelity ratios using Sec. III-A.
    return {
        "reviewer_line4", "detailed", 6,
        {4, 4, 4, 4},
        {length_from_fidelity_ratio(0.92),
         length_from_fidelity_ratio(0.84),
         length_from_fidelity_ratio(0.94)},
        0.80
    };
}

vector<TrialSpec> random_trials(int count, uint32_t seed) {
    mt19937 generator(seed);
    // These ranges stay around the nontrivial reviewer instance.  Capacity 3
    // and very long links make R2 structurally infeasible in five slots and
    // primarily test admission at a degenerate boundary, not (W,P) quality.
    uniform_real_distribution<double> length_distribution(6.0, 18.0);
    uniform_real_distribution<double> threshold_distribution(0.80, 0.84);
    uniform_int_distribution<int> capacity_distribution(4, 5);
    uniform_int_distribution<int> horizon_distribution(5, 6);

    vector<TrialSpec> trials;
    trials.reserve(count);
    for(int index = 0; index < count; ++index) {
        TrialSpec trial;
        ostringstream name;
        name << "random_" << setw(3) << setfill('0') << index + 1;
        trial.name = name.str();
        trial.kind = "random";
        trial.time_limit = horizon_distribution(generator);
        for(int node = 0; node < 4; ++node) {
            trial.capacities.push_back(capacity_distribution(generator));
        }
        for(int link = 0; link < 3; ++link) {
            trial.lengths_km.push_back(length_distribution(generator));
        }
        trial.fidelity_threshold = threshold_distribution(generator);
        trials.push_back(std::move(trial));
    }
    return trials;
}

void write_results_header(ofstream& output) {
    output
        << "instance,kind,seed,nodes,edges,requests,time_limit,"
        << "capacities_v1_v4,lengths_km_l12_l23_l34,fidelity_threshold,"
        << "epsilon,bucket_eps,max_purification_rounds,algorithm,"
        << "proven_optimal,objective,optimality_gap_pct,accepted_requests,"
        << "expected_requests,purified_requests,runtime_ms,"
        << "enumerated_schedules,feasible_schedules,"
        << "nondominated_candidates,search_states\n";
}

void write_result_row(ofstream& output,
                      const Comparison& result,
                      uint32_t seed,
                      bool exact) {
    const TrialSpec& spec = result.spec;
    output << spec.name << ',' << spec.kind << ',' << seed
           << ",4,3,3," << spec.time_limit << ','
           << join_ints(spec.capacities) << ','
           << join_doubles(spec.lengths_km) << ','
           << spec.fidelity_threshold << ','
           << SMALL_SCALE_EPSILON << ',' << SMALL_SCALE_BUCKET_EPS << ','
           << SMALL_SCALE_MAX_PURIFICATION_ROUNDS << ',';
    if(exact) {
        output << "OPT,1," << result.optimum.objective << ",0,"
               << result.optimum.accepted_requests << ','
               << result.optimum.expected_requests << ','
               << result.optimum_purified_requests << ','
               << result.exact_runtime_ms << ','
               << result.optimum.enumerated_schedules << ','
               << result.optimum.feasible_schedules << ','
               << result.optimum.nondominated_candidates << ','
               << result.optimum.search_states << '\n';
    } else {
        output << "WPFA,0," << result.wpfa.objective << ','
               << result.gap_pct << ','
               << result.wpfa.accepted_requests << ','
               << result.wpfa.expected_requests << ','
               << result.wpfa_purified_requests << ','
               << result.wpfa.runtime_ms << ",,,,\n";
    }
}

bool is_exact_match(const Comparison& result) {
    const double scale = max(1.0, fabs(result.optimum.objective));
    return fabs(result.optimum.objective - result.wpfa.objective) <=
           MATCH_TOLERANCE * scale;
}

double quantile(vector<double> values, double probability) {
    if(values.empty()) return numeric_limits<double>::quiet_NaN();
    sort(values.begin(), values.end());
    const double position = probability * (values.size() - 1);
    const size_t lower = (size_t)floor(position);
    const size_t upper = (size_t)ceil(position);
    const double fraction = position - lower;
    return values[lower] * (1.0 - fraction) + values[upper] * fraction;
}

void write_batch_summary(const string& filename,
                         const vector<Comparison>& results,
                         uint32_t seed) {
    ofstream output(filename);
    if(!output) throw runtime_error("cannot create summary: " + filename);
    output << setprecision(17);
    output
        << "random_instances,seed,length_min_km,length_max_km,"
        << "threshold_min,threshold_max,capacity_min,capacity_max,"
        << "horizon_min,horizon_max,average_gap_pct,median_gap_pct,"
        << "p95_gap_pct,maximum_gap_pct,worst_instance,exact_matches,"
        << "positive_opt_instances,opt_instances_using_purification,"
        << "mean_exact_runtime_ms,mean_wpfa_runtime_ms\n";

    if(results.empty()) {
        output << "0," << seed
               << ",6,18,0.80,0.84,4,5,5,6,"
               << "nan,nan,nan,nan,none,0,0,0,nan,nan\n";
        return;
    }

    vector<double> gaps;
    gaps.reserve(results.size());
    double gap_sum = 0.0;
    double exact_runtime_sum = 0.0;
    double wpfa_runtime_sum = 0.0;
    int exact_matches = 0;
    int positive_optimum = 0;
    int purification_instances = 0;
    size_t worst = 0;
    for(size_t index = 0; index < results.size(); ++index) {
        const Comparison& result = results[index];
        gaps.push_back(result.gap_pct);
        gap_sum += result.gap_pct;
        exact_runtime_sum += result.exact_runtime_ms;
        wpfa_runtime_sum += result.wpfa.runtime_ms;
        if(is_exact_match(result)) ++exact_matches;
        if(result.optimum.objective > OBJECTIVE_TOLERANCE) ++positive_optimum;
        if(result.optimum_purified_requests > 0) ++purification_instances;
        if(result.gap_pct > results[worst].gap_pct) worst = index;
    }

    output << results.size() << ',' << seed
           << ",6,18,0.80,0.84,4,5,5,6,"
           << gap_sum / results.size() << ','
           << quantile(gaps, 0.50) << ',' << quantile(gaps, 0.95) << ','
           << results[worst].gap_pct << ',' << results[worst].spec.name << ','
           << exact_matches << ',' << positive_optimum << ','
           << purification_instances << ','
           << exact_runtime_sum / results.size() << ','
           << wpfa_runtime_sum / results.size() << '\n';
}

CaseSpec canonical_case_spec(const TrialSpec& trial) {
    return {
        trial.name, 4, trial.time_limit, 4,
        {{0, 1, fidelity_ratio_from_length(trial.lengths_km[0])},
         {1, 2, fidelity_ratio_from_length(trial.lengths_km[1])},
         {2, 3, fidelity_ratio_from_length(trial.lengths_km[2])}},
        reviewer_requests()
    };
}

void write_detailed_artifacts(const string& answer_directory,
                              const Comparison& result) {
    const CaseSpec spec = canonical_case_spec(result.spec);
    vector<SelectionEntry> opt_entries = exact_entries(result.optimum);
    vector<SelectionEntry> wpfa_entries = algorithm_entries(result.wpfa);
    for(SelectionEntry& entry : wpfa_entries) entry.algorithm = "WPFA";

    const string selection_path =
        answer_directory + "/main_smallscale_selection.csv";
    const string numerology_path =
        answer_directory + "/main_smallscale_numerology.csv";
    const string schedule_path =
        answer_directory + "/main_smallscale_schedules.txt";
    const string certificate_path =
        answer_directory + "/main_smallscale_opt_certificate.txt";

    ofstream selection(selection_path);
    ofstream numerology(numerology_path);
    ofstream schedule(schedule_path);
    ofstream certificate(certificate_path);
    if(!selection || !numerology || !schedule || !certificate) {
        throw runtime_error("cannot create detailed small-scale artifacts");
    }
    selection << setprecision(17);
    certificate << setprecision(17);
    write_selection_header(selection);
    write_selection_rows(selection, spec, opt_entries);
    write_selection_rows(selection, spec, wpfa_entries);
    write_numerology_header(numerology);
    write_numerology_rows(numerology, spec, "OPT", opt_entries);
    write_numerology_rows(numerology, spec, "WPFA", wpfa_entries);
    write_schedule_report(schedule, spec, "OPT", result.optimum.objective,
                          result.optimum.objective, opt_entries);
    write_schedule_report(schedule, spec, "WPFA", result.wpfa.objective,
                          result.optimum.objective, wpfa_entries);
    write_certificate(certificate, spec, result.optimum);
}

int total_memory_units(const vector<SelectionEntry>& entries,
                       int time_limit) {
    int total = 0;
    for(const SelectionEntry& entry : entries) {
        total += make_numerology(
            4, time_limit, entry.shape_vector, entry.purify_rounds).total_units;
    }
    return total;
}

void print_comparison(const Comparison& result) {
    cout << '[' << result.spec.name << "] OPT=" << result.optimum.objective
         << " WPFA=" << result.wpfa.objective
         << " gap=" << result.gap_pct << "%"
         << " accepted=" << result.optimum.accepted_requests << '/'
         << result.wpfa.accepted_requests
         << " purified=" << result.optimum_purified_requests << '/'
         << result.wpfa_purified_requests << '\n';
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

        ofstream results_csv(results_path);
        if(!results_csv) {
            throw runtime_error("cannot create results: " + results_path);
        }
        results_csv << setprecision(17);
        write_results_header(results_csv);

        cout << fixed << setprecision(6)
             << "Four-node line: exact OPT versus WPFA\n"
             << "requests=(1,3),(1,4),(2,4), epsilon="
             << SMALL_SCALE_EPSILON
             << ", bucket_eps=" << SMALL_SCALE_BUCKET_EPS << '\n';

        const Comparison detailed = compare_trial(
            canonical_trial(), input_directory);
        write_result_row(results_csv, detailed, options.seed, true);
        write_result_row(results_csv, detailed, options.seed, false);
        write_detailed_artifacts(answer_directory, detailed);
        print_comparison(detailed);

        vector<SelectionEntry> detailed_opt = exact_entries(detailed.optimum);
        vector<SelectionEntry> detailed_wpfa =
            algorithm_entries(detailed.wpfa);
        cout << "  detailed memory-slot units: OPT="
             << total_memory_units(detailed_opt, detailed.spec.time_limit)
             << " WPFA="
             << total_memory_units(detailed_wpfa, detailed.spec.time_limit)
             << '\n';

        vector<Comparison> random_results;
        for(const TrialSpec& trial : random_trials(
                options.random_instances, options.seed)) {
            Comparison result = compare_trial(trial, input_directory);
            write_result_row(results_csv, result, options.seed, true);
            write_result_row(results_csv, result, options.seed, false);
            print_comparison(result);
            random_results.push_back(std::move(result));
        }
        results_csv.close();

        write_batch_summary(summary_path, random_results, options.seed);
        if(!random_results.empty()) {
            double gap_sum = 0.0;
            int matches = 0;
            size_t worst = 0;
            for(size_t index = 0; index < random_results.size(); ++index) {
                gap_sum += random_results[index].gap_pct;
                if(is_exact_match(random_results[index])) ++matches;
                if(random_results[index].gap_pct >
                   random_results[worst].gap_pct) {
                    worst = index;
                }
            }
            cout << "Random-batch summary: average gap="
                 << gap_sum / random_results.size()
                 << "%, maximum gap=" << random_results[worst].gap_pct
                 << "% (" << random_results[worst].spec.name << ')'
                 << ", exact matches=" << matches << '/'
                 << random_results.size() << '\n';
        }
        cout << "Results: " << results_path << '\n'
             << "Summary: " << summary_path << '\n'
             << "Detailed schedules and numerology: "
             << answer_directory << "/main_smallscale_*\n";
        return 0;
    } catch(const exception& error) {
        cerr << "main_smallscale: " << error.what() << '\n';
        return 1;
    }
}
