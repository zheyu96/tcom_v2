#ifndef REPS_PATH_METHOD_H
#define REPS_PATH_METHOD_H

#include "../PathMethodBase/PathMethod.h"

using namespace std;

class REPS : public PathMethod {
    using DirectedEdge = pair<int, int>;
    using FlowMap = map<DirectedEdge, double>;

    void solve_fractional_flow(vector<double>& throughput,
                               vector<FlowMap>& flows);
    pair<Path, double> widest_path(int src,
                                   int dst,
                                   const FlowMap& flow);
    static void subtract_path_flow(const Path& path,
                                   double amount,
                                   FlowMap& flow);

public:
    REPS();
    ~REPS();

    // True only when REPS.cpp was compiled with -DREPS_USE_GUROBI.
    static bool gurobi_available();
    void build_paths(Graph graph, vector<SDpair> requests) override;
};

#endif
