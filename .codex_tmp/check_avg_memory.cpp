
#define main runtime_driver_main
#include "../src/main_time.cpp"
#undef main
#include <cassert>
int main() {
    Config defaults;
    assert(defaults.sweeps.size() == 11);
    assert(defaults.avg_memories == vector<double>({4,6,8,10,12,14,16,20}));
    char a0[]="check", a1[]="--sweeps", a2[]="avg_memory", a3[]="--avg-memories", a4[]="4,10,20";
    char* args[]={a0,a1,a2,a3,a4};
    Config parsed=parse_arguments(5,args);
    assert(parsed.sweeps == vector<string>({"avg_memory"}));
    assert(values_for_sweep(parsed,AVG_MEMORY) == vector<double>({4,10,20}));
    for(double memory: parsed.avg_memories) {
        auto settings=settings_for(AVG_MEMORY,memory);
        assert(settings.avg_memory==memory && settings.request_count==100);
        assert(settings.time_limit==13 && settings.fidelity_threshold==.8);
    }
    assert(settings_for(TIME_LIMIT,7).avg_memory==10);
    for(string invalid: {"0","-1","4.5","2147483648"}) {
        args[4]=invalid.data();
        bool rejected=false;
        try { (void)parse_arguments(5,args); } catch(const invalid_argument&) { rejected=true; }
        assert(rejected);
    }
    Graph small=load_graph("data/input/main_smallscale_request_cnt_4_line3.input",13,.8,4);
    Graph large=load_graph("data/input/main_smallscale_request_cnt_4_line3.input",13,.8,10);
    for(int node=0;node<3;++node) assert(large.get_node_memory(node)-small.get_node_memory(node)==6);
    assert(settings_for("tao",.003).tao == .003);
    assert(settings_for("swap_prob",.7).swap_prob == .7);
    assert(settings_for("hop_count",4).hop_count == 4);
    assert(settings_for("mem_vary",3).mem_vary == 3);
    assert(settings_for("topo_vary",2).topo_vary == 2);
    assert(settings_for("mem_distribution",1).mem_distribution == 1);
    assert(settings_for("path_set",1).path_set == 1);
    auto qcast_paths = build_paths(small, vector<SDpair>{{0,2},{0,1}}, 1);
    assert(!qcast_paths.at({0,2}).empty());
    bool reps_rejected=false;
    try { build_paths(small, vector<SDpair>{{0,2}}, 2); }
    catch(const runtime_error&) { reps_rejected=true; }
    assert(reps_rejected);
    Graph physical = load_graph("data/input/main_smallscale_request_cnt_4_line3.input",7,.8,4,.003,.7);
    assert(physical.get_time_limit()==7);
    assert(abs(physical.get_node_swap_prob(0)-.7)<1e-12);
    parsed.output_directory=".codex_tmp";
    parsed.algorithms={"ZFA2"};
    vector<Sample> samples;
    for(double memory:parsed.avg_memories) {
        for(double seconds:{1.0,3.0,8.0}) {
            Sample sample;
            sample.sweep=AVG_MEMORY; sample.parameter_value=memory;
            sample.algorithm="ZFA2"; sample.seconds=seconds;
            samples.push_back(sample);
        }
    }
    write_chart_files(parsed,samples);
    write_summary(parsed,samples);
    cout << "Validated memory sweep parsing, ranges, unchanged defaults, graph capacities and median output.\n";
}
