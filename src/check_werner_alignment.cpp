// Build with build_werner_alignment_check.py. Uses the same runtime workloads.
#define main shared_runtime_main
#include "main_time.cpp"
#undef main
#include ".compile_check/werner_before_alignment/BeforeWernerAlgo.h"
#include ".compile_check/werner_before_alignment/BeforeWernerAlgo2.h"
#ifdef double
#undef double
#endif

namespace {
void verify_feasible(Graph graph, const AlgorithmBase& algorithm) {
    const int weights[4][5] = {{1,1}, {1,2,2}, {1,2,3,2}, {1,2,3,3,2}};
    for(const auto& record : algorithm.get_accepted_shapes()) {
        Shape shape(record.node_mem_range, record.purify_rounds);
        shape.check_valid();
        if(!graph.check_resource(shape, true, true))
            throw runtime_error("accepted schedule violates fidelity/base memory");
        map<pair<int,int>, int> need;
        for(const auto& node : record.node_mem_range)
            for(auto range : node.second)
                for(int t = range.first; t <= range.second; ++t)
                    ++need[{node.first, t}];
        for(size_t edge = 0; edge < record.purify_rounds.size(); ++edge) {
            int rounds = record.purify_rounds[edge];
            if(rounds == 0) continue;
            if(rounds < 0 || rounds > 3) throw runtime_error("invalid purification");
            int start = record.node_mem_range[edge].second.back().first;
            for(int dt = 0; dt <= rounds + 1; ++dt) {
                int extra = weights[rounds][rounds + 1 - dt] - 1;
                if(extra <= 0) continue;
                need[{record.node_mem_range[edge].first, start + dt}] += extra;
                need[{record.node_mem_range[edge + 1].first, start + dt}] += extra;
            }
        }
        for(const auto& slot : need) {
            if(slot.first.second < 0 || slot.first.second >= graph.get_time_limit() ||
               graph.get_node_memory_at(slot.first.first, slot.first.second) < slot.second)
                throw runtime_error("accepted schedule violates total purification memory");
        }
        for(const auto& slot : need)
            graph.reserve_node_memory_at(slot.first.first, slot.first.second, slot.second);
    }
}

void verify_same(const AlgorithmBase& a, const AlgorithmBase& b) {
    const auto& left = a.get_res();
    const auto& right = b.get_res();
    if(left.size() != right.size()) throw runtime_error("metric keys differ");
    for(const auto& metric : left) {
        double value = right.at(metric.first);
        if(metric.second != value && !(isnan(metric.second) && isnan(value)))
            throw runtime_error("zero-purification metric differs: " + metric.first);
    }
    if(a.get_cdf() != b.get_cdf()) throw runtime_error("zero-purification CDF differs");
    const auto& x = a.get_accepted_shapes();
    const auto& y = b.get_accepted_shapes();
    if(x.size() != y.size()) throw runtime_error("zero-purification count differs");
    for(size_t i = 0; i < x.size(); ++i) {
        if(x[i].node_mem_range != y[i].node_mem_range ||
           x[i].purify_rounds != y[i].purify_rounds || x[i].fidelity != y[i].fidelity ||
           x[i].success_probability != y[i].success_probability ||
           x[i].expected_werner != y[i].expected_werner)
            throw runtime_error("zero-purification accepted schedule differs");
    }
}
}

int main(int argc, char** argv) {
    try {
        Config config = parse_arguments(argc, argv);
        omp_set_dynamic(0);
        omp_set_num_threads(config.threads);
        const auto workloads = prepare_workloads(config);
        ofstream output(join_path(config.output_directory, "werner_quality.csv"));
        if(!output) throw runtime_error("cannot open quality output");
        output << "sweep,parameter_value,instance,variant,fidelity_gain,succ_request_cnt,"
                  "actual_req_cnt,accepted_schedules,feasible,oracle_calls,dp_paths,"
                  "peak_candidates,peak_labels\n" << setprecision(17);
        size_t verified = 0;
        for(const auto& sweep : config.sweeps) {
            for(double value : values_for_sweep(config, sweep)) {
                auto settings = settings_for(sweep, value);
                for(int instance = 0; instance < config.instances; ++instance) {
                    Graph graph = load_graph(workloads[instance].input_file,
                        settings.time_limit, settings.fidelity_threshold);
                    vector<SDpair> requests(workloads[instance].request_pool.begin(),
                        workloads[instance].request_pool.begin() + settings.request_count);
                    auto paths = build_paths(graph, requests);
                    auto zfa = make_algorithm("ZFA", graph, requests, paths, "audit",
                                              config.epsilon, config.bucket_eps, config.oracle_reuse, config.reuse_cost_growth);
                    WernerAlgo2 zero(graph, requests, paths, config.epsilon, config.bucket_eps, 0);
                    zero.set_detailed_logging(false);
                    zero.set_oracle_reuse(config.oracle_reuse, config.reuse_cost_growth);
                    for(AlgorithmBase* algorithm : {zfa.get(), static_cast<AlgorithmBase*>(&zero)}) {
                        algorithm->set_record_accepted_shapes(true);
                        ScopedQuietStreams quiet(true);
                        algorithm->run();
                    }
                    verify_same(*zfa, zero);
                    for(const string& variant : {"old_ZFA", "old_WPFA", "baseline_ZFA", "baseline_WPFA", "ZFA", "WPFA"}) {
                        unique_ptr<AlgorithmBase> owned;
                        AlgorithmBase* algorithm;
                        if(variant == "ZFA") algorithm = zfa.get();
                        else {
                            if(variant == "baseline_ZFA" || variant == "baseline_WPFA")
                                owned = make_algorithm(variant == "baseline_ZFA" ? "ZFA" : "ZFA2",
                                    graph, requests, paths, "baseline", config.epsilon, config.bucket_eps, 1,
                                    config.reuse_cost_growth);
                            else if(variant == "old_ZFA")
                                owned = make_unique<BeforeWernerAlgo>(graph, requests, paths, config.epsilon, config.bucket_eps);
                            else if(variant == "old_WPFA") {
                                auto old = make_unique<BeforeWernerAlgo2>(graph, requests, paths, config.epsilon, config.bucket_eps);
                                old->set_detailed_logging(false);
                                owned = move(old);
                            } else owned = make_algorithm("ZFA2", graph, requests, paths, "audit", config.epsilon, config.bucket_eps, config.oracle_reuse, config.reuse_cost_growth);
                            algorithm = owned.get();
                            algorithm->set_record_accepted_shapes(true);
                            ScopedQuietStreams quiet(true);
                            algorithm->run();
                        }
                        verify_feasible(graph, *algorithm);
                        size_t calls = 0, dp_paths = 0, candidates = 0, labels = 0;
                        if(auto* werner = dynamic_cast<WernerAlgo2*>(algorithm)) {
                            const auto& stats = werner->get_solver_stats();
                            calls = stats.oracle_calls; dp_paths = stats.dp_paths;
                            candidates = stats.peak_candidates; labels = stats.peak_labels;
                            if(stats.dual_updates > stats.oracle_calls * werner->get_oracle_reuse())
                                throw runtime_error("oracle reuse exceeds configured limit");
                            if(variant == "ZFA")
                                for(const auto& record : algorithm->get_accepted_shapes())
                                    for(int rounds : record.purify_rounds)
                                        if(rounds != 0) throw runtime_error("ZFA purified a link");
                        }
                        output << sweep << ',' << value << ',' << instance << ',' << variant << ','
                               << algorithm->get_res("fidelity_gain") << ','
                               << algorithm->get_res("succ_request_cnt") << ','
                               << algorithm->get_res("actual_req_cnt") << ','
                               << algorithm->get_accepted_shapes().size() << ",1,"
                               << calls << ',' << dp_paths << ',' << candidates << ',' << labels << '\n';
                        output.flush();
                        ++verified;
                    }
                    cout << sweep << '=' << value << " instance=" << instance
                         << " zero-mode identical; all schedules feasible\n" << flush;
                }
            }
        }
        cout << "Verified " << verified << " solver runs\n";
        return 0;
    } catch(const exception& error) {
        cerr << "[Werner alignment] " << error.what() << '\n';
        return 1;
    }
}
