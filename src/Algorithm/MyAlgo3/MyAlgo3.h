#ifndef __MYALGO3_H
#define __MYALGO3_H

#include    "../AlgorithmBase/AlgorithmBase.h"
#include    "../../Network/Graph/Graph.h"
#include    "../../config.h"

#include <array>

using namespace std;

class MyAlgo3 : public AlgorithmBase {
    vector<vector<vector<array<double, 4>>>> dp, dp2;
    vector<vector<vector<array<pair<int, int>, 4>>>> par, par2;
    vector<vector<vector<array<bool, 4>>>> caled, caled2;
    pair<Shape, double> calculate_best_shape(int src, int dst);
    pair<Shape, double> calculate_best_shape2(int src, int dst);
    double solve_fidelity(int left, int right, int t, int state, const vector<int> &path);
    double solve2(int left, int right, int t, int state, const vector<int> &path);
    double cp_value(Shape& shape);
    Shape_vector backtracing_shape(int left, int right, int t, int state, const vector<int> &path);
    Shape_vector backtracing_shape2(int left, int right, int t, int state, const vector<int> &path);
public:
    MyAlgo3(const Graph& graph, const vector<pair<int, int>>& requests, const map<SDpair, vector<Path>>& paths);
    void run();
};

#endif