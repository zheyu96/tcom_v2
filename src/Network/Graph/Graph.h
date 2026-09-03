#ifndef __GRAPH_H
#define __GRAPH_H

#include "../Node/Node.h"
#include "../Shape/Shape.h"
#include "../../config.h"

using namespace std;
using Path = vector<int>;
using SDpair = pair<int, int>;

// Audit trail of one schedule that an algorithm actually reserved.  Recording
// is opt-in (Graph::set_record_accepted_shapes) so that the large-scale
// experiments keep their original behaviour and memory footprint.
struct AcceptedShapeRecord {
    int src = -1;
    int dst = -1;
    Shape_vector node_mem_range;
    vector<int> purify_rounds;
    double fidelity = 0.0;
    double success_probability = 0.0;
    double expected_werner = 0.0;   // Werner(fidelity) * success_probability
};


class Graph {
    int num_nodes;
    int time_limit;
    double fidelity_threshold;
    double A, B, n, T, tao;
    // Paper III-A: distance-dependent decoherence/entangling.
    //   F_e(u,v) = 1/4 + 3/4 * exp(-Gamma * l(u,v))
    //   Pr(u,v)  = 1 - (1 - exp(-lambda * l(u,v)))^xi,  xi = floor(tao / entangle_time)
    double entangle_lambda;   // lambda  (paper, fiber-loss)
    double entangle_time;     // tau_att (paper, per-attempt time)
    double Gamma;             // Gamma   (paper, decoherence per km)
    double Zmin,bucket_eps,time_eta;
    double fidelity_gain;
    double swapping_succ_prob;
    double pure_fidelity;
    double actual_req_cnt=0;
    double avg_entangle_prob;
    int usage;
    double succ_request_cnt;
    double delta_P;
    vector<Node> nodes;

    vector<double> boundary, cnt;
    map<pair<int, int> , double> F_init, entangle_succ_prob;

    string file_name;
    Path get_path(int from, int to);
    bool record_accepted_shapes_enabled = false;
    vector<AcceptedShapeRecord> accepted_shape_records;
    void record_accepted_shape(Shape& shape, double shape_fidelity, double pr);
public:
    Graph(string filename, int _time_limit, double _swap_prob, int avg_memory, double min_fidelity, double max_fidelity, double _fidelity_threshold, double _A, double _B, double _n, double _T, double _tao,double _Zmin,double _bucket_eps,double _time_eta,double _delta_P=0.0,double _entangle_lambda=0.045,double _entangle_time=0.00025,double _Gamma=0.0044);
    Graph() {}
    ~Graph();
    int get_node_memory_at(int node_id, int t);
    int get_node_memory(int node_id);
    double get_node_swap_prob(int node_id);
    int get_num_nodes();
    int get_time_limit();
    double get_succ_request_cnt();
    double get_actual_req_cnt();
    int get_memory_total();
    int get_usage();
    double get_Zmin();
    double get_bucket_eps();
    double get_time_eta();
    double get_A();
    double get_B();
    double get_n();
    double get_T();
    double get_tao();
    double get_fidelity_gain();
    double get_entangle_succ_prob(int u, int v);
    double get_fidelity_threshold();
    double get_pure_fidelity();
    double get_F_init(int u, int v);
    double get_delta_P();
    // Physical-layer constants of Sec. III-A, exposed so that reports and
    // figures quote the values the model actually ran with.
    double get_Gamma();
    double get_entangle_lambda();
    double get_entangle_time();
    int get_entangle_attempts();      // xi = floor(delta / tau_att)
    const map<pair<int, int>, double>& get_F_init() const;

    double get_link_werner(int u,int v);
    double get_edge_W(int u,int v);
    const vector<double>& get_boundary() const;
    const vector<double>& get_cnt() const;

    // [關鍵修改] 這裡必須宣告帶有 enable_purification 的版本
    bool check_resource(Shape shape, bool threshold = true, bool enable_purification = false);
    bool check_resource_ASAP(Shape shape, bool threshold = true, bool enable_purification = false);
    
    // [關鍵修改] 這裡也必須宣告
    void reserve_shape(Shape shape, bool enable_purification = false);
    void reserve_shape2(Shape shape, bool enable_purification = false);
    void reserve_shape_ASAP(Shape shape, bool enable_purification = false);

    double path_Pr(Path path);
    double path_Pr(const Shape& shape);
    double path_Pr_purify(const Shape& shape);
    bool check_path_resource(Path path, int amount);
    void reserve_path(Path path);
    void reserve_path(Path path, int amount);
    int distance(int src, int dst);
    double get_ini_fid(int u,int v);
    void reserve_node_memory_at(int node_id, int t, int amount);
    void set_record_accepted_shapes(bool enabled);
    const vector<AcceptedShapeRecord>& get_accepted_shapes() const;
    void increase_resources(int multi);
    vector<vector<int>> adj_list;
    vector<set<int>> adj_set;
};

#endif