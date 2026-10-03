#ifndef __WERNER_ALGO_H
#define __WERNER_ALGO_H

#include "../WernerAlgo2/WernerAlgo2.h"

// WPFA ablation: identical solver, with link purification disabled.
class WernerAlgo : public WernerAlgo2 {
public:
    WernerAlgo(const Graph& graph,
               const vector<pair<int, int>>& requests,
               const map<SDpair, vector<Path>>& paths,
               double epsilon = EXPERIMENT_EPSILON,
               double bucket_eps = 0.001);
};

#endif
