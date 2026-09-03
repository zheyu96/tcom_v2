#ifndef __ALGORITHMBASE_H
#define __ALGORITHMBASE_H

#include "../../Network/Graph/Graph.h"
#include "../../Network/PathMethod/PathMethodBase/PathMethod.h"
#include "../../config.h"
using namespace std;


class AlgorithmBase {
protected:
    string algorithm_name;
    Graph graph;
    map<string, double> res;
    vector<double> cdf;
    vector<SDpair> requests;
    int time_limit, memory_total, request_cnt;
    map<SDpair, vector<Path>> paths;
    void update_res();
    double A, B, n, T, tao;
    double bar(double F);
    double Fswap(double Fa, double Fb);
    double t2F(double t);
    double F2t(double F);
    double pass_tao(double F);
    const vector<Path>& get_paths(int src, int dst);
public:
    AlgorithmBase(const Graph& graph, const vector<SDpair>& requests, const map<SDpair, vector<Path>>& paths);
    const map<string, double>& get_res() const;
    double get_res(string str);
    const vector<double>& get_cdf() const;
    string get_name();
    // Opt-in audit trail of the schedules this algorithm reserved.  Enable
    // before run(); the records are exactly the accepted schedules that the
    // reported objective was accumulated from.
    void set_record_accepted_shapes(bool enabled) {
        graph.set_record_accepted_shapes(enabled);
    }
    const vector<AcceptedShapeRecord>& get_accepted_shapes() const {
        return graph.get_accepted_shapes();
    }
    virtual ~AlgorithmBase();
    virtual void run() = 0;
};

#endif