#ifndef __EFIRAP_H
#define __EFIRAP_H

// WernerAlgo*.h defines `double` as a preprocessor macro and does not restore
// it. Isolate EFiRAP's public ABI from that leaking macro so EFiRAP.h has the
// same constructor signature regardless of include order.
#ifdef double
#pragma push_macro("double")
#undef double
#define EFIRAP_RESTORE_DOUBLE_MACRO
#endif

#include "../AlgorithmBase/AlgorithmBase.h"
#include "../../Network/Graph/Graph.h"
#include "../../config.h"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace std;

/**
 * EFiRAP adapted to this simulator's physical model.
 *
 * Paper components retained:
 *   - EPP uses Yen's K-shortest paths with link weight -log(F_l).
 *   - Purification repeatedly selects the link with the largest marginal
 *     end-to-end fidelity gain and preserves all exact-tie branches.
 *   - EPS maximizes the number of admitted connections under resource
 *     constraints.
 *
 * Confirmed simulator adaptations:
 *   - End-to-end fidelity and purification are evaluated by Shape and the
 *     project's Werner/decoherence model, not the paper's bit-flip model.
 *   - There is no independent link-capacity constraint. One primary Bell
 *     pair and every sacrificial pair consume memory at both link endpoints.
 *   - Purification uses the same per-round duration and per-timeslot memory
 *     profile as WPFA. Memory is reusable after the operation lifetime ends.
 *   - EPS follows Algorithm 2 in the paper: solve the LP relaxation, enumerate
 *     bounded integer lower-bound guesses, re-solve the LP, and floor it.
 */
class EFiRAP : public AlgorithmBase {
public:
    EFiRAP(const Graph& graph,
           const vector<SDpair>& requests,
           const map<SDpair, vector<Path>>& paths,
           int k_paths = 10,
           double approximation_epsilon = 0.5,
           double solver_time_limit_seconds = 0.0,
           long long enumeration_state_limit = 100000);

    void run() override;

    // True only when EFiRAP.cpp is compiled with -DEFIRAP_USE_GUROBI.
    static bool gurobi_available();

private:
    using ResourceKey = pair<int, int>; // (node, time)

    struct RequestGroup {
        SDpair sd;
        int demand = 0;
        vector<Path> yen_paths;
    };

    struct Candidate {
        Path path;
        vector<int> purify_rounds;
        Shape_vector shape_vector;
        map<ResourceKey, int> memory_usage;
        double fidelity = 0.0;
        double success_probability = 0.0;
        int start_time = 0;
    };

    struct PurificationScheme {
        vector<int> rounds;
        double fidelity = 0.0;
    };

    int k_paths;
    double approximation_epsilon;
    double solver_time_limit_seconds;
    // Algorithm 2 is exponential. The paper explicitly permits trying only a
    // subset of guesses in large instances. Zero requests exhaustive search.
    long long enumeration_state_limit;
    long long initial_memory_capacity = 0;

    vector<RequestGroup> request_groups;
    vector<vector<Candidate>> candidates;

    void build_request_groups();
    void prepare_candidates();

    vector<Path> yen_k_shortest_paths(int src, int dst, int K);
    Path shortest_path(int src,
                       int dst,
                       const set<int>& banned_nodes,
                       const set<pair<int, int>>& banned_edges);
    double path_cost(const Path& path);

    int assign_balanced_swap_times(int left,
                                   int right,
                                   int start_time,
                                   const vector<int>& purify_rounds,
                                   vector<int>& swap_time) const;
    Shape_vector build_balanced_shape(
        const Path& path,
        const vector<int>& purify_rounds,
        int start_time);

    vector<PurificationScheme> prepare_path_schemes(const Path& path);
    double evaluate_fidelity(const Shape_vector& shape_vector,
                             const vector<int>& purify_rounds) const;
    map<ResourceKey, int> calculate_memory_usage(
        const Shape_vector& shape_vector,
        const vector<int>& purify_rounds) const;
    bool fits_initial_memory(const map<ResourceKey, int>& usage);
    bool fits_current_memory(const map<ResourceKey, int>& usage);

    vector<vector<int>> solve_eps_ptas_with_gurobi();
    void reserve_candidate(const Candidate& candidate);
};

#ifdef EFIRAP_RESTORE_DOUBLE_MACRO
#pragma pop_macro("double")
#undef EFIRAP_RESTORE_DOUBLE_MACRO
#endif

#endif
