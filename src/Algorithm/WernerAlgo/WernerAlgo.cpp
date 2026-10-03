#include "WernerAlgo.h"

WernerAlgo::WernerAlgo(const Graph& graph,
                       const vector<pair<int, int>>& requests,
                       const map<SDpair, vector<Path>>& paths,
                       double epsilon, double bucket_eps)
    : WernerAlgo2(graph, requests, paths, epsilon, bucket_eps, 0) {}
