#include "ExperimentWorkload.h"

#include "config.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <queue>
#include <random>
#include <set>

using namespace std;

// This generator originally appeared after WernerAlgo2.h in main.cpp, whose
// historical macro made all of these calculations long double. Keep that
// precision explicit now that the workload code is a standalone module.
using WorkloadReal = long double;

vector<SDpair> generate_stratified_requests(
    Graph &graph,
    int requests_cnt,
    RequestGenerationConfig config) {
    const int node_count = graph.get_num_nodes();
    const WorkloadReal threshold = graph.get_fidelity_threshold();
    const WorkloadReal A = graph.get_A(), B = graph.get_B();
    const WorkloadReal n_param = graph.get_n();
    const WorkloadReal T = graph.get_T(), tao = graph.get_tao();
    const int max_purification_rounds = 3;
    const array<int, 3> target_hops{{2, 3, 4}};

    auto shortest_path = [&](int source, int destination) -> Path {
        vector<int> parent(node_count, -1);
        queue<int> pending;
        parent[source] = source;
        pending.push(source);
        while(!pending.empty() && parent[destination] == -1) {
            int node = pending.front();
            pending.pop();
            for(int next : graph.adj_list[node]) {
                if(parent[next] != -1) continue;
                parent[next] = node;
                pending.push(next);
            }
        }
        if(parent[destination] == -1) return {};

        Path path;
        for(int node = destination; node != source; node = parent[node])
            path.push_back(node);
        path.push_back(source);
        reverse(path.begin(), path.end());
        return path;
    };

    auto t2F = [&](WorkloadReal time) -> WorkloadReal {
        if(time >= 1e5) return 0.0;
        return A + B * exp(-pow(time / T, n_param));
    };
    auto F2t = [&](WorkloadReal fidelity) -> WorkloadReal {
        if(fidelity <= A + 1e-9) return 1e9;
        return T * pow(-log((fidelity - A) / B), 1.0 / n_param);
    };
    auto pass_tao = [&](WorkloadReal fidelity) -> WorkloadReal {
        return t2F(F2t(fidelity) + tao);
    };
    auto swap_fidelity = [&](WorkloadReal left, WorkloadReal right) -> WorkloadReal {
        if(left <= A + 1e-9 || right <= A + 1e-9) return 0.0;
        return left * right
             + (1.0 / 3.0) * (1.0 - left) * (1.0 - right);
    };

    function<WorkloadReal(int, int, const vector<WorkloadReal>&, const vector<int>&)>
        balanced_fidelity;
    balanced_fidelity = [&] (
        int left,
        int right,
        const vector<WorkloadReal>& link_fidelity,
        const vector<int>& purification_rounds) -> WorkloadReal {
        if(right == left + 1) {
            WorkloadReal raw = link_fidelity[left];
            WorkloadReal purified = raw;
            for(int round = 0; round < purification_rounds[left]; ++round) {
                const WorkloadReal current_bar = 1.0 - purified;
                const WorkloadReal raw_bar = 1.0 - raw;
                const WorkloadReal denominator =
                    purified * raw
                    + (1.0 / 3.0) * purified * raw_bar
                    + (1.0 / 3.0) * current_bar * raw
                    + (5.0 / 9.0) * current_bar * raw_bar;
                const WorkloadReal numerator =
                    purified * raw + (1.0 / 9.0) * current_bar * raw_bar;
                purified = numerator / denominator;
            }
            return pass_tao(purified);
        }

        const int middle = (left + right) / 2;
        const WorkloadReal left_fidelity = balanced_fidelity(
            left, middle, link_fidelity, purification_rounds);
        const WorkloadReal right_fidelity = balanced_fidelity(
            middle, right, link_fidelity, purification_rounds);
        return swap_fidelity(
            pass_tao(left_fidelity), pass_tao(right_fidelity));
    };

    struct CandidateProfile {
        SDpair request;
        Path path;
        vector<int> feasible_two_hop_intermediates;
        WorkloadReal base_fidelity;
        int minimum_purification_rounds;
        int hops;
    };
    struct ScoredRequest {
        SDpair request;
        WorkloadReal base_fidelity;
        int pressure;
        bool crosses_hotspot;
        int minimum_purification_rounds;
        int hops;
    };

    config.minimum_hops = max(1, config.minimum_hops);
    config.maximum_hops = max(config.minimum_hops, config.maximum_hops);
    vector<CandidateProfile> candidate_profiles;
    vector<int> common_transit_load(node_count, 0);
    vector<int> all_transit_load(node_count, 0);
    // total, common, one-round, two-round, excluded-three-round
    map<int, array<int, 5>> diagnostics;

    for(int source = 0; source < node_count; ++source) {
        for(int destination = source + 1; destination < node_count;
            ++destination) {
            Path path = shortest_path(source, destination);
            const int hops = (int)path.size() - 1;
            if(hops < config.minimum_hops || hops > config.maximum_hops)
                continue;

            vector<WorkloadReal> link_fidelity(hops);
            for(int link = 0; link < hops; ++link) {
                link_fidelity[link] = graph.get_F_init(
                    path[link], path[link + 1]);
            }

            vector<int> no_purification(hops, 0);
            const WorkloadReal base_fidelity = balanced_fidelity(
                0, hops, link_fidelity, no_purification);
            diagnostics[hops][0]++;

            // Enumerate every threshold-feasible two-hop route.  A hotspot
            // request is useful for comparing path/numerology algorithms only
            // when the same SD pair also has a feasible route around the
            // hotspot; considering just one BFS path hides that trade-off.
            vector<int> feasible_two_hop_intermediates;
            Path best_common_path;
            WorkloadReal best_common_fidelity = -1.0;
            if(config.minimum_hops <= 2 && config.maximum_hops >= 2) {
                for(int intermediate : graph.adj_list[source]) {
                    if(!graph.adj_set[intermediate].count(destination))
                        continue;
                    vector<WorkloadReal> two_hop_fidelity{
                        graph.get_F_init(source, intermediate),
                        graph.get_F_init(intermediate, destination)};
                    vector<int> no_two_hop_purification(2, 0);
                    const WorkloadReal candidate_fidelity = balanced_fidelity(
                        0, 2, two_hop_fidelity,
                        no_two_hop_purification);
                    if(candidate_fidelity + EPS < threshold) continue;
                    feasible_two_hop_intermediates.push_back(intermediate);
                    if(candidate_fidelity > best_common_fidelity) {
                        best_common_fidelity = candidate_fidelity;
                        best_common_path = {
                            source, intermediate, destination};
                    }
                }
            }

            if(!feasible_two_hop_intermediates.empty()) {
                candidate_profiles.push_back({
                    {source, destination}, best_common_path,
                    feasible_two_hop_intermediates,
                    best_common_fidelity, 0, 2});
                for(int intermediate : feasible_two_hop_intermediates) {
                    all_transit_load[intermediate]++;
                    if(feasible_two_hop_intermediates.size() >= 2)
                        common_transit_load[intermediate]++;
                }
                diagnostics[hops][1]++;
                continue;
            }

            // Longer shortest paths become common-feasible when the generated
            // links are sufficiently good.  Keeping them is important: paths
            // of length at least three are the first ones with genuinely
            // different swapping-tree/numerology resource profiles.
            if(base_fidelity + EPS >= threshold) {
                candidate_profiles.push_back({
                    {source, destination}, path, {}, base_fidelity, 0, hops});
                for(int position = 1;
                    position + 1 < (int)path.size(); ++position) {
                    const int intermediate = path[position];
                    common_transit_load[intermediate]++;
                    all_transit_load[intermediate]++;
                }
                diagnostics[hops][1]++;
                continue;
            }

            int minimum_purification_rounds = -1;
            for(int rounds = 1; rounds <= max_purification_rounds; ++rounds) {
                vector<int> purification(hops, rounds);
                if(balanced_fidelity(0, hops, link_fidelity, purification)
                   >= threshold) {
                    minimum_purification_rounds = rounds;
                    break;
                }
            }
            if(minimum_purification_rounds > 0) {
                if(minimum_purification_rounds == 3) {
                    diagnostics[hops][4]++;
                    continue;
                }
                candidate_profiles.push_back({
                    {source, destination}, path, {}, base_fidelity,
                    minimum_purification_rounds, hops});
                for(int position = 1; position + 1 < (int)path.size();
                    ++position)
                    all_transit_load[path[position]]++;
                diagnostics[hops][1 + minimum_purification_rounds]++;
            }
        }
    }

    // Select a small number of topology-derived bottlenecks.  Common-feasible
    // traffic drives this ranking because WPFA-noPurify, FNPR, and FLTO can all
    // serve that traffic.  Fall back to the full feasible pool when necessary.
    const vector<int>& hotspot_load = *max_element(
        common_transit_load.begin(), common_transit_load.end()) > 0
        ? common_transit_load
        : all_transit_load;
    vector<int> node_order(node_count);
    for(int node = 0; node < node_count; ++node) node_order[node] = node;
    sort(node_order.begin(), node_order.end(), [&](int left, int right) {
        if(hotspot_load[left] != hotspot_load[right])
            return hotspot_load[left] > hotspot_load[right];
        return left < right;
    });
    set<int> hotspot_nodes;
    const int hotspot_node_limit = min(
        max(0, config.hotspot_node_limit), node_count);
    for(int position = 0; position < hotspot_node_limit; ++position) {
        const int node = node_order[position];
        if(hotspot_load[node] <= 0) break;
        hotspot_nodes.insert(node);
    }

    vector<ScoredRequest> common_candidates_by_fidelity;
    array<vector<ScoredRequest>, 3> common_candidates_by_hop;
    array<vector<ScoredRequest>, REQUEST_STRATUM_COUNT> stratum_candidates;
    auto hop_index = [&](int hops) -> int {
        for(int index = 0; index < (int)target_hops.size(); ++index) {
            if(target_hops[index] == hops) return index;
        }
        return -1;
    };

    for(const CandidateProfile& profile : candidate_profiles) {
        const int profile_hop_index = hop_index(profile.hops);
        if(profile_hop_index < 0) continue;
        int pressure = 0;
        bool crosses_hotspot = false;
        bool has_background_alternative = false;
        vector<int> relevant_intermediates =
            profile.feasible_two_hop_intermediates;
        if(relevant_intermediates.empty()) {
            for(int position = 1;
                position + 1 < (int)profile.path.size(); ++position)
                relevant_intermediates.push_back(profile.path[position]);
        }
        for(int node : relevant_intermediates) {
            pressure = max(pressure, hotspot_load[node]);
            crosses_hotspot = crosses_hotspot || hotspot_nodes.count(node);
            has_background_alternative = has_background_alternative
                || !hotspot_nodes.count(node);
        }

        auto add_orientation = [&](SDpair request) {
            ScoredRequest candidate{
                request, profile.base_fidelity, pressure,
                crosses_hotspot
                    && (profile.minimum_purification_rounds > 0
                        || has_background_alternative),
                profile.minimum_purification_rounds,
                profile.hops};
            if(profile.minimum_purification_rounds == 0) {
                common_candidates_by_fidelity.push_back(candidate);
                common_candidates_by_hop[profile_hop_index].push_back(
                    candidate);
            } else if(profile.minimum_purification_rounds == 1) {
                stratum_candidates[ONE_ROUND_PURIFICATION].push_back(
                    candidate);
            } else if(profile.minimum_purification_rounds == 2) {
                stratum_candidates[TWO_ROUND_PURIFICATION].push_back(
                    candidate);
            }
        };
        add_orientation(profile.request);
        add_orientation({profile.request.second, profile.request.first});
    }

    // Split common-feasible candidates within each hop bucket.  Splitting per
    // hop prevents the high-fidelity pool from degenerating into only two-hop
    // requests and keeps the requested 2/3/4-hop mix achievable.
    sort(
        common_candidates_by_fidelity.begin(),
        common_candidates_by_fidelity.end(),
        [](const ScoredRequest& left, const ScoredRequest& right) {
            if(left.base_fidelity != right.base_fidelity)
                return left.base_fidelity < right.base_fidelity;
            if(left.pressure != right.pressure)
                return left.pressure > right.pressure;
            return left.request < right.request;
        });
    const int common_split =
        (int)common_candidates_by_fidelity.size() / 2;
    for(int hop = 0; hop < (int)target_hops.size(); ++hop) {
        auto& candidates = common_candidates_by_hop[hop];
        sort(candidates.begin(), candidates.end(),
             [](const ScoredRequest& left, const ScoredRequest& right) {
                 if(left.base_fidelity != right.base_fidelity)
                     return left.base_fidelity < right.base_fidelity;
                 if(left.pressure != right.pressure)
                     return left.pressure > right.pressure;
                 return left.request < right.request;
             });
        const int tail_size = candidates.empty()
            ? 0
            : max(1, (int)ceil(candidates.size() * 0.40));
        for(int index = 0; index < tail_size; ++index) {
            stratum_candidates[MARGINAL_COMMON].push_back(candidates[index]);
            stratum_candidates[HIGH_COMMON].push_back(
                candidates[candidates.size() - 1 - index]);
        }
    }

    int total_candidate_count = 0;
    for(const auto& candidates : stratum_candidates)
        total_candidate_count += candidates.size();
    if(total_candidate_count == 0) {
        cerr << "[request_mix] ERROR: no feasible 2--4-hop stress requests"
             << endl;
        return {};
    }

    mt19937 generator(config.random_seed);
    config.hotspot_candidate_limit = max(1, config.hotspot_candidate_limit);
    config.minimum_repetitions = max(1, config.minimum_repetitions);
    config.maximum_repetitions = max(
        config.minimum_repetitions, config.maximum_repetitions);
    uniform_int_distribution<int> repeat_distribution(
        config.minimum_repetitions, config.maximum_repetitions);

    struct SamplingPool {
        vector<ScoredRequest> candidates;
        int position = 0;
        int repetitions_left = 0;
        ScoredRequest current;
    };
    // Last dimension: 0=background, 1=hotspot.
    array<array<array<SamplingPool, 2>, 3>, REQUEST_STRATUM_COUNT>
        sampling_pools;

    for(int stratum = 0; stratum < REQUEST_STRATUM_COUNT; ++stratum) {
        for(const ScoredRequest& candidate : stratum_candidates[stratum]) {
            const int candidate_hop_index = hop_index(candidate.hops);
            if(candidate_hop_index < 0) continue;
            const int pressure_class = candidate.crosses_hotspot ? 1 : 0;
            sampling_pools[stratum][candidate_hop_index][pressure_class]
                .candidates.push_back(candidate);
        }
        for(int hop = 0; hop < 3; ++hop) {
            auto& hotspot_pool = sampling_pools[stratum][hop][1].candidates;
            sort(hotspot_pool.begin(), hotspot_pool.end(),
                 [](const ScoredRequest& left, const ScoredRequest& right) {
                     if(left.pressure != right.pressure)
                         return left.pressure > right.pressure;
                     if(left.base_fidelity != right.base_fidelity)
                         return left.base_fidelity > right.base_fidelity;
                     return left.request < right.request;
                 });
            if((int)hotspot_pool.size() > config.hotspot_candidate_limit)
                hotspot_pool.resize(config.hotspot_candidate_limit);
            shuffle(hotspot_pool.begin(), hotspot_pool.end(), generator);
            auto& background_pool =
                sampling_pools[stratum][hop][0].candidates;
            shuffle(background_pool.begin(), background_pool.end(), generator);
        }
    }

    auto draw_from_pool = [&](int stratum, int hop, int pressure_class,
                              ScoredRequest& selected) -> bool {
        SamplingPool& pool = sampling_pools[stratum][hop][pressure_class];
        if(pool.candidates.empty()) return false;
        if(pool.repetitions_left == 0) {
            if(pool.position >= (int)pool.candidates.size()) {
                shuffle(pool.candidates.begin(), pool.candidates.end(), generator);
                pool.position = 0;
            }
            pool.current = pool.candidates[pool.position++];
            pool.repetitions_left = repeat_distribution(generator);
        }
        selected = pool.current;
        pool.repetitions_left--;
        return true;
    };

    int pressure_fallbacks = 0;
    int hop_fallbacks = 0;
    int stratum_fallbacks = 0;
    auto draw_with_fallback = [&] (
        int requested_stratum,
        int requested_hop,
        bool want_hotspot,
        ScoredRequest& selected,
        int& actual_stratum,
        int& actual_hop) -> bool {
        const int requested_pressure = want_hotspot ? 1 : 0;
        if(draw_from_pool(requested_stratum, requested_hop,
                          requested_pressure, selected)) {
            actual_stratum = requested_stratum;
            actual_hop = requested_hop;
            return true;
        }
        if(draw_from_pool(requested_stratum, requested_hop,
                          1 - requested_pressure, selected)) {
            pressure_fallbacks++;
            actual_stratum = requested_stratum;
            actual_hop = requested_hop;
            return true;
        }
        for(int hop = 0; hop < 3; ++hop) {
            if(hop == requested_hop) continue;
            for(int pressure : {requested_pressure, 1 - requested_pressure}) {
                if(draw_from_pool(requested_stratum, hop, pressure, selected)) {
                    hop_fallbacks++;
                    actual_stratum = requested_stratum;
                    actual_hop = hop;
                    return true;
                }
            }
        }
        for(int stratum = 0; stratum < REQUEST_STRATUM_COUNT; ++stratum) {
            if(stratum == requested_stratum) continue;
            for(int pressure : {requested_pressure, 1 - requested_pressure}) {
                if(draw_from_pool(stratum, requested_hop, pressure, selected)) {
                    stratum_fallbacks++;
                    actual_stratum = stratum;
                    actual_hop = requested_hop;
                    return true;
                }
            }
        }
        for(int stratum = 0; stratum < REQUEST_STRATUM_COUNT; ++stratum) {
            for(int hop = 0; hop < 3; ++hop) {
                for(int pressure = 0; pressure <= 1; ++pressure) {
                    if(draw_from_pool(stratum, hop, pressure, selected)) {
                        stratum_fallbacks++;
                        hop_fallbacks++;
                        actual_stratum = stratum;
                        actual_hop = hop;
                        return true;
                    }
                }
            }
        }
        return false;
    };

    // The request-count experiment consumes prefixes of this pool.  The 20-
    // request template has exact 35/25/30/10 stratum and 30/60/10 hop ratios,
    // so every 80/100/... prefix preserves the intended workload composition.
    vector<SDpair> requests;
    requests.reserve(requests_cnt);
    array<array<int, 3>, REQUEST_STRATUM_COUNT> actual_mix{};
    array<int, REQUEST_STRATUM_COUNT> stratum_slots_seen{};
    array<int, REQUEST_STRATUM_COUNT> actual_hotspot_by_stratum{};
    int selected_hotspot_count = 0;
    while((int)requests.size() < requests_cnt) {
        const int block_size = min(20, requests_cnt - (int)requests.size());
        array<array<int, 3>, REQUEST_STRATUM_COUNT> block_mix{};
        struct FractionalCell {
            WorkloadReal remainder;
            int stratum;
            int hop;
        };
        vector<FractionalCell> fractional_cells;
        int assigned = 0;
        for(int stratum = 0; stratum < REQUEST_STRATUM_COUNT; ++stratum) {
            for(int hop = 0; hop < 3; ++hop) {
                const WorkloadReal exact =
                    config.stratum_hop_fraction[stratum][hop] * block_size;
                block_mix[stratum][hop] = (int)floor(exact + 1e-9);
                assigned += block_mix[stratum][hop];
                fractional_cells.push_back({
                    exact - floor(exact + 1e-9), stratum, hop});
            }
        }
        sort(fractional_cells.begin(), fractional_cells.end(),
             [](const FractionalCell& left, const FractionalCell& right) {
                 if(left.remainder != right.remainder)
                     return left.remainder > right.remainder;
                 if(left.stratum != right.stratum)
                     return left.stratum < right.stratum;
                 return left.hop < right.hop;
             });
        for(int index = 0; assigned < block_size; ++index, ++assigned) {
            const FractionalCell& cell =
                fractional_cells[index % fractional_cells.size()];
            block_mix[cell.stratum][cell.hop]++;
        }

        vector<pair<int, int>> block;
        block.reserve(block_size);
        for(int stratum = 0; stratum < REQUEST_STRATUM_COUNT; ++stratum) {
            for(int hop = 0; hop < 3; ++hop) {
                for(int copy = 0; copy < block_mix[stratum][hop]; ++copy)
                    block.push_back({stratum, hop});
            }
        }
        shuffle(block.begin(), block.end(), generator);
        for(const auto& [requested_stratum, requested_hop] : block) {
            const int next_stratum_count =
                stratum_slots_seen[requested_stratum] + 1;
            const WorkloadReal hotspot_fraction = min(
                1.0L,
                max(0.0L, config.hotspot_fraction[requested_stratum]));
            const int desired_hotspot_count = (int)lround(
                next_stratum_count * hotspot_fraction);
            const bool want_hotspot =
                actual_hotspot_by_stratum[requested_stratum]
                    < desired_hotspot_count;

            ScoredRequest selected;
            int actual_stratum = -1, actual_hop = -1;
            if(!draw_with_fallback(
                   requested_stratum, requested_hop, want_hotspot,
                   selected, actual_stratum, actual_hop)) {
                cerr << "[request_mix] ERROR: all sampling pools are empty"
                     << endl;
                return {};
            }
            requests.push_back(selected.request);
            stratum_slots_seen[requested_stratum]++;
            if(selected.crosses_hotspot)
                actual_hotspot_by_stratum[requested_stratum]++;
            selected_hotspot_count += selected.crosses_hotspot;
            actual_mix[actual_stratum][actual_hop]++;
        }
    }

    const array<const char*, REQUEST_STRATUM_COUNT> stratum_names{{
        "marginal", "high", "purify-1", "purify-2"
    }};
    cerr << "[request_mix] threshold=" << threshold
         << " T=" << T << " tao=" << tao
         << " hops=" << config.minimum_hops << "--" << config.maximum_hops
         << " | stress workload seed=" << config.random_seed << endl;
    cerr << "  achieved stratum/hop mix:" << endl;
    for(int stratum = 0; stratum < REQUEST_STRATUM_COUNT; ++stratum) {
        int stratum_total = 0;
        cerr << "    " << stratum_names[stratum] << ":";
        for(int hop = 0; hop < 3; ++hop) {
            stratum_total += actual_mix[stratum][hop];
            cerr << " " << target_hops[hop] << "hop="
                 << actual_mix[stratum][hop];
        }
        cerr << " total=" << stratum_total << endl;
    }
    if(!common_candidates_by_fidelity.empty()) {
        const WorkloadReal minimum_common_fidelity =
            common_candidates_by_fidelity.front().base_fidelity;
        const WorkloadReal median_common_fidelity =
            common_candidates_by_fidelity[common_split].base_fidelity;
        const WorkloadReal maximum_common_fidelity =
            common_candidates_by_fidelity.back().base_fidelity;
        cerr << "  common fidelity range=" << minimum_common_fidelity
             << " ... " << median_common_fidelity
             << " ... " << maximum_common_fidelity << endl;
    }
    cerr << "  hotspot nodes:";
    for(int node : hotspot_nodes)
        cerr << " v" << node << "(candidate_load=" << hotspot_load[node]
              << ")";
    cerr << " | selected hotspot=" << selected_hotspot_count << "/"
         << requests.size() << endl;
    cerr << "  fallbacks pressure=" << pressure_fallbacks
         << " hop=" << hop_fallbacks
         << " stratum=" << stratum_fallbacks << endl;

    int peak_internal_node_load = 0;
    vector<int> selected_internal_load(node_count, 0);
    set<SDpair> unique_requests;
    for(const SDpair& request : requests) {
        unique_requests.insert(request);
        Path path = shortest_path(request.first, request.second);
        for(int position = 1; position + 1 < (int)path.size(); ++position) {
            const int node = path[position];
            peak_internal_node_load = max(
                peak_internal_node_load, ++selected_internal_load[node]);
        }
    }
    cerr << "  unique directed SD pairs="
         << unique_requests.size() << " | peak internal-node demand="
         << peak_internal_node_load << endl;
    for(const auto& [hops, count] : diagnostics) {
        cerr << "  hop=" << hops
             << " candidates=" << count[0]
             << " common=" << count[1]
             << " purify-1=" << count[2]
             << " purify-2=" << count[3]
             << " excluded-purify-3=" << count[4] << endl;
    }
    return requests;
}
