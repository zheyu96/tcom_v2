// Differential benchmark: original and optimized implementations receive the
// exact same main_runtime workloads. Build with build_fnpr_flto_benchmark.py.
#define main shared_runtime_main
#include "main_time.cpp"
#undef main

#include ".compile_check/fnpr_flto_baseline/BaselineMyAlgo1.h"
#include ".compile_check/fnpr_flto_baseline/BaselineMyAlgo3.h"

namespace {
bool same_number(double a, double b) {
    return a == b || (isnan(a) && isnan(b));
}

void check_results(const AlgorithmBase& original, const AlgorithmBase& optimized,
                   bool check_schedules) {
    const auto& left = original.get_res();
    const auto& right = optimized.get_res();
    if(left.size() != right.size()) throw runtime_error("result key mismatch");
    for(const auto& item : left) {
        auto found = right.find(item.first);
        if(found == right.end() || !same_number(item.second, found->second))
            throw runtime_error("result mismatch: " + item.first);
    }
    if(original.get_cdf() != optimized.get_cdf())
        throw runtime_error("CDF mismatch");
    if(!check_schedules) return;
    const auto& a = original.get_accepted_shapes();
    const auto& b = optimized.get_accepted_shapes();
    if(a.size() != b.size()) throw runtime_error("accepted count mismatch");
    for(size_t i = 0; i < a.size(); ++i) {
        if(a[i].src != b[i].src || a[i].dst != b[i].dst ||
           a[i].node_mem_range != b[i].node_mem_range ||
           a[i].purify_rounds != b[i].purify_rounds ||
           !same_number(a[i].fidelity, b[i].fidelity) ||
           !same_number(a[i].success_probability, b[i].success_probability) ||
           !same_number(a[i].expected_werner, b[i].expected_werner))
            throw runtime_error("accepted schedule mismatch at " + to_string(i));
    }
}

unique_ptr<AlgorithmBase> reference_algorithm(
    const string& name, const Graph& graph, const vector<SDpair>& requests,
    const map<SDpair, vector<Path>>& paths, double epsilon) {
    if(name == "MyAlgo1") {
        auto algorithm = make_unique<BaselineMyAlgo1>(graph, requests, paths);
        algorithm->set_epsilon(epsilon);
        return algorithm;
    }
    return make_unique<BaselineMyAlgo3>(graph, requests, paths);
}

double measure(AlgorithmBase& algorithm) {
    ScopedQuietStreams quiet(true);
    auto start = chrono::steady_clock::now();
    algorithm.run();
    return chrono::duration<double>(chrono::steady_clock::now() - start).count();
}
}

int main(int argc, char** argv) {
    try {
        Config config = parse_arguments(argc, argv);
        // The benchmark intentionally compares only these two implementations.
        config.algorithms = {"MyAlgo1", "MyAlgo3"};
        omp_set_dynamic(0);
        omp_set_num_threads(config.threads);
        const auto workloads = prepare_workloads(config);
        ofstream raw(join_path(config.output_directory, "fnpr_flto_raw.csv"));
        ofstream summary(join_path(config.output_directory, "fnpr_flto_summary.csv"));
        ofstream quality(join_path(config.output_directory, "fnpr_flto_quality.csv"));
        if(!raw || !summary || !quality) throw runtime_error("cannot open outputs");
        raw << "sweep,parameter_value,instance,repetition,algorithm,epsilon,threads,"
               "original_seconds,optimized_seconds,fidelity_gain,succ_request_cnt,"
               "actual_req_cnt,metrics_identical\n";
        summary << "sweep,parameter_value,algorithm,samples,original_mean_seconds,"
                   "optimized_mean_seconds,speedup,original_median_seconds,"
                   "optimized_median_seconds\n";
        quality << "sweep,parameter_value,instance,algorithm,accepted_schedules,"
                   "metrics_identical,cdf_identical,schedules_identical\n";
        raw << setprecision(17);
        summary << setprecision(17);
        size_t checked = 0, measured = 0;
        for(const string& sweep : config.sweeps) {
            for(double value : values_for_sweep(config, sweep)) {
                auto settings = settings_for(sweep, value);
                map<string, vector<pair<double, double>>> samples;
                for(int instance = 0; instance < config.instances; ++instance) {
                    const auto& workload = workloads[instance];
                    Graph graph = load_graph(workload.input_file, settings.time_limit,
                                             settings.fidelity_threshold);
                    vector<SDpair> requests(workload.request_pool.begin(),
                        workload.request_pool.begin() + settings.request_count);
                    const auto paths = build_paths(graph, requests);
                    for(const string& name : config.algorithms) {
                        // Full schedule validation is separate from timed runs,
                        // and doubles as the first warmup for each variant.
                        auto old = reference_algorithm(name, graph, requests, paths, config.epsilon);
                        auto next = make_algorithm(name, graph, requests, paths, "audit",
                                                   config.epsilon, config.bucket_eps);
                        old->set_record_accepted_shapes(true);
                        next->set_record_accepted_shapes(true);
                        (void)measure(*old);
                        (void)measure(*next);
                        check_results(*old, *next, true);
                        quality << sweep << ',' << value << ',' << instance << ',' << name
                                << ',' << old->get_accepted_shapes().size() << ",1,1,1\n";
                        quality.flush();
                        ++checked;
                        for(int warmup = 1; warmup < config.warmups; ++warmup) {
                            auto a = reference_algorithm(name, graph, requests, paths, config.epsilon);
                            auto b = make_algorithm(name, graph, requests, paths, "warmup",
                                                    config.epsilon, config.bucket_eps);
                            (void)measure(*a);
                            (void)measure(*b);
                        }
                        for(int repetition = 0; repetition < config.repetitions; ++repetition) {
                            auto a = reference_algorithm(name, graph, requests, paths, config.epsilon);
                            auto b = make_algorithm(name, graph, requests, paths, "timed",
                                                    config.epsilon, config.bucket_eps);
                            double before, after;
                            if((instance + repetition) % 2 == 0) {
                                before = measure(*a);
                                after = measure(*b);
                            } else {
                                after = measure(*b);
                                before = measure(*a);
                            }
                            check_results(*a, *b, false);
                            samples[name].push_back({before, after});
                            raw << sweep << ',' << value << ',' << instance << ','
                                << repetition << ',' << name << ',' << config.epsilon
                                << ',' << config.threads << ',' << before << ',' << after
                                << ',' << b->get_res("fidelity_gain") << ','
                                << b->get_res("succ_request_cnt") << ','
                                << b->get_res("actual_req_cnt") << ",1\n";
                            raw.flush();
                            ++measured;
                        }
                    }
                    cout << sweep << '=' << value << " instance=" << instance
                         << " quality identical\n" << flush;
                }
                for(const auto& entry : samples) {
                    vector<double> before, after;
                    for(auto sample : entry.second) {
                        before.push_back(sample.first);
                        after.push_back(sample.second);
                    }
                    double old_mean = accumulate(before.begin(), before.end(), 0.0) / before.size();
                    double new_mean = accumulate(after.begin(), after.end(), 0.0) / after.size();
                    sort(before.begin(), before.end());
                    sort(after.begin(), after.end());
                    auto median = [](const vector<double>& v) {
                        return (v[(v.size() - 1) / 2] + v[v.size() / 2]) / 2;
                    };
                    summary << sweep << ',' << value << ',' << entry.first << ','
                            << before.size() << ',' << old_mean << ',' << new_mean
                            << ',' << old_mean / new_mean << ',' << median(before)
                            << ',' << median(after) << '\n';
                    summary.flush();
                }
            }
        }
        cout << "Validated " << checked << " schedule pairs and " << measured
             << " timed result pairs\n";
        return 0;
    } catch(const exception& error) {
        cerr << "[FNPR/FLTO] " << error.what() << '\n';
        return 1;
    }
}
