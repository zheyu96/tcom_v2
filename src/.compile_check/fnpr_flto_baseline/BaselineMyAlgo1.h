#ifndef __BASELINE_MYALGO1_H
#define __BASELINE_MYALGO1_H

#include "../../Algorithm/AlgorithmBase/AlgorithmBase.h"
#include "../../Network/Graph/Graph.h"
#include "../../config.h"

using namespace std;

class BaselineMyAlgo1 : public AlgorithmBase {
    vector<double> alpha;
    vector<vector<double>> beta;
    vector<map<Shape_vector, double>> x;
    vector<vector<vector<double>>> dp;
    vector<vector<vector<bool>>> caled;
    vector<vector<vector<int>>> par;
    double epsilon = EXPERIMENT_EPSILON, obj;
    void variable_initialize();
    Shape_vector separation_oracle();
    pair<Shape_vector, double> find_min_shape(int src, int dst, double alp);
    double recursion_calculate_min_shape(int left, int right, int t, const vector<int> &path);
    Shape_vector recursion_find_shape(int left, int right, int t, const vector<int> &path);
public:
    BaselineMyAlgo1(const Graph& graph, const vector<pair<int, int>>& requests, const map<SDpair, vector<Path>>& paths);
    void set_epsilon(double value) { epsilon = value; }
    void run();
};

#endif