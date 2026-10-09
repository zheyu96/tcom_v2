
#define main runtime_driver_main
#include "../src/main_time.cpp"
#undef main
#include <cassert>
int main() {
    Config defaults;
    assert(defaults.sweeps.back() == "avg_memory");
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
