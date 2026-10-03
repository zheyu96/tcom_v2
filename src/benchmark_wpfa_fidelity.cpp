// Alternating paired measurements with exact schedule/metric verification.
#define main shared_runtime_main
#include "main_time.cpp"
#undef main
#include ".compile_check/wpfa_fidelity_reference/BeforeWernerAlgo2.h"
#ifdef double
#undef double
#endif
namespace {
void verify_same(const AlgorithmBase& a, const AlgorithmBase& b) {
    const auto& left = a.get_res();
    const auto& right = b.get_res();
    if(left.size() != right.size()) throw runtime_error("metric keys differ");
    for(const auto& metric : left) {
        double value = right.at(metric.first);
        if(metric.second != value && !(isnan(metric.second) && isnan(value)))
            throw runtime_error("WPFA before/after metric differs: " + metric.first);
    }
    if(a.get_cdf() != b.get_cdf()) throw runtime_error("WPFA before/after CDF differs");
    const auto& x = a.get_accepted_shapes();
    const auto& y = b.get_accepted_shapes();
    if(x.size() != y.size()) throw runtime_error("WPFA before/after count differs");
    for(size_t i = 0; i < x.size(); ++i) {
        if(x[i].node_mem_range != y[i].node_mem_range ||
           x[i].purify_rounds != y[i].purify_rounds || x[i].fidelity != y[i].fidelity ||
           x[i].success_probability != y[i].success_probability ||
           x[i].expected_werner != y[i].expected_werner)
            throw runtime_error("WPFA before/after accepted schedule differs");
    }
}
}
int main(int argc, char** argv) {
 try {
    // A baseline-vs-baseline control distinguishes existing parallel
    // nondeterminism from changes introduced by the optimization.
    bool baseline_control = false;
    vector<char*> filtered_args;
    for(int i = 0; i < argc; ++i) {
        if(string(argv[i]) == "--baseline-control") baseline_control = true;
        else filtered_args.push_back(argv[i]);
    }
    Config config = parse_arguments((int)filtered_args.size(), filtered_args.data());
    omp_set_dynamic(0); omp_set_num_threads(config.threads);
    auto workloads = prepare_workloads(config);
    ofstream out(join_path(config.output_directory, "wpfa_fidelity_raw.csv"));
    if(!out) throw runtime_error("cannot open benchmark output");
    out << "threshold,instance,repetition,variant,paths_seconds,construction_seconds,run_seconds,total_seconds,fidelity_gain,accepted_schedules,dp_paths,peak_candidates,peak_labels\n" << setprecision(17);
    for(double threshold : config.fidelity_thresholds) {
      for(int instance=0; instance<config.instances; ++instance) {
        auto graph = load_graph(workloads[instance].input_file,13,threshold);
        vector<SDpair> requests(workloads[instance].request_pool.begin(),workloads[instance].request_pool.begin()+100);
        for(int rep=-config.warmups;rep<config.repetitions;++rep) {
          unique_ptr<AlgorithmBase> before,after;
          for(int order=0;order<2;++order) {
            bool old = (order == ((instance+rep+config.warmups)%2));
            auto total_start=chrono::steady_clock::now();
            auto paths=build_paths(graph,requests);
            auto paths_finish=chrono::steady_clock::now();
            unique_ptr<AlgorithmBase> algorithm;
            if(old || baseline_control) {
              auto ptr=make_unique<BeforeWernerAlgo2>(graph,requests,paths,config.epsilon,config.bucket_eps);
              ptr->set_detailed_logging(false);ptr->set_oracle_reuse(config.oracle_reuse,config.reuse_cost_growth);
              algorithm=std::move(ptr);
            } else algorithm=make_algorithm("ZFA2",graph,requests,paths,"fidelity-benchmark",config.epsilon,config.bucket_eps,config.oracle_reuse,config.reuse_cost_growth);
            auto construction_finish=chrono::steady_clock::now();
            algorithm->set_record_accepted_shapes(true);
            { ScopedQuietStreams quiet(true);algorithm->run(); }
            auto finish=chrono::steady_clock::now();
            size_t dp_paths,peak_candidates,peak_labels;
            if(old || baseline_control) {auto stats=static_cast<BeforeWernerAlgo2*>(algorithm.get())->get_solver_stats();dp_paths=stats.dp_paths;peak_candidates=stats.peak_candidates;peak_labels=stats.peak_labels;}
            else {auto stats=static_cast<WernerAlgo2*>(algorithm.get())->get_solver_stats();dp_paths=stats.dp_paths;peak_candidates=stats.peak_candidates;peak_labels=stats.peak_labels;}
            if(rep>=0) out << threshold << ',' << instance << ',' << rep << ',' << (old?"before":"after") << ','
              << chrono::duration<double>(paths_finish-total_start).count() << ','
              << chrono::duration<double>(construction_finish-paths_finish).count() << ','
              << chrono::duration<double>(finish-construction_finish).count() << ','
              << chrono::duration<double>(finish-total_start).count() << ','
              << algorithm->get_res("fidelity_gain") << ',' << algorithm->get_accepted_shapes().size() << ',' << dp_paths << ',' << peak_candidates << ',' << peak_labels << '\n';
            if(old) before=std::move(algorithm);else after=std::move(algorithm);
          }
          verify_same(*before,*after);
          out.flush();
        }
        cout << "threshold=" << threshold << " instance=" << instance << " exact comparison passed\n";
      }
    }
    return 0;
 } catch(const exception& e) {cerr << e.what() << '\n';return 1;}
}
