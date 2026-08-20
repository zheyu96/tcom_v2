#ifndef EXPERIMENT_WORKLOAD_H
#define EXPERIMENT_WORKLOAD_H

#include <array>
#include <vector>

#include "Network/Graph/Graph.h"

enum RequestStratum {
    MARGINAL_COMMON = 0,
    HIGH_COMMON = 1,
    ONE_ROUND_PURIFICATION = 2,
    TWO_ROUND_PURIFICATION = 3,
    REQUEST_STRATUM_COUNT = 4
};

struct RequestGenerationConfig {
    int minimum_hops = 2;
    int maximum_hops = 4;
    int hotspot_node_limit = 2;
    int hotspot_candidate_limit = 8;
    int minimum_repetitions = 1;
    int maximum_repetitions = 3;
    unsigned int random_seed = 20260820U;

    // Rows: marginal, high, one-round purification, two-round purification.
    // Columns: 2, 3, and 4 hops.
    std::array<std::array<long double, 3>, REQUEST_STRATUM_COUNT>
        stratum_hop_fraction{{
            {{0.15, 0.20, 0.00}},
            {{0.15, 0.10, 0.00}},
            {{0.00, 0.20, 0.10}},
            {{0.00, 0.10, 0.00}}
        }};

    std::array<long double, REQUEST_STRATUM_COUNT> hotspot_fraction{{
        0.50, 0.30, 0.60, 0.60
    }};
};

std::vector<SDpair> generate_stratified_requests(
    Graph& graph,
    int requests_cnt,
    RequestGenerationConfig config = RequestGenerationConfig());

#endif
