#ifndef __MYALGO1_H
#define __MYALGO1_H

#include "../AlgorithmBase/AlgorithmBase.h"
#include "../../Network/Graph/Graph.h"
#include "../../config.h"

#include <stdexcept>

using namespace std;

class MyAlgo1 : public AlgorithmBase {
    vector<double> alpha;
    vector<vector<double>> beta;
    vector<map<Shape_vector, double>> x;
    vector<vector<vector<double>>> dp;
    vector<vector<vector<bool>>> caled;
    vector<vector<vector<int>>> par;
    double epsilon = EXPERIMENT_EPSILON, obj;
    // Same primal-dual engineering as WPFA: bounded oracle reuse and a lazy
    // oracle over SD-pair groups. The DP depends only on beta, so it is run
    // once per group and every request in the group is scored from it.
    int oracle_reuse = EXPERIMENT_ORACLE_REUSE;
    double reuse_cost_growth = EXPERIMENT_REUSE_COST_GROWTH;
    struct PathEvaluation {
        double probability = 1.0;
        vector<double> cost;            // DP value for each end slot t
        vector<vector<vector<int>>> par; // DP parents, backtracked on demand
        vector<Shape_vector> shape;     // memoized shape per end slot t
        vector<unsigned char> has_shape;
    };
    struct RequestGroup {
        vector<int> nodes;
        vector<int> request_indices;
        vector<PathEvaluation> paths;
        bool stale = true;
        double lower_bound = -1.0;      // < 0 means never computed
    };
    struct RequestEvaluation {
        Shape_vector shape;
        double value = 0.0;
    };
    vector<RequestGroup> groups;
    vector<int> request_group;
    vector<RequestEvaluation> request_cache;
    vector<unsigned char> dirty_nodes;
    vector<unsigned char> dirty_alpha;
    void variable_initialize();
    Shape_vector separation_oracle();
    void recompute_group(RequestGroup& group);
    RequestEvaluation evaluate_group(RequestGroup& group, double alp);
    double recursion_calculate_min_shape(int left, int right, int t, const vector<int> &path);
    Shape_vector recursion_find_shape(int left, int right, int t, const vector<int> &path);
public:
    MyAlgo1(const Graph& graph, const vector<pair<int, int>>& requests, const map<SDpair, vector<Path>>& paths);
    void set_epsilon(double value) { epsilon = value; }
    void set_oracle_reuse(int n, double growth = EXPERIMENT_REUSE_COST_GROWTH) {
        if(n < 1 || !isfinite(growth) || growth < 0) throw invalid_argument("invalid reuse controls");
        oracle_reuse = n; reuse_cost_growth = growth;
    }
    void run();
};

#endif
