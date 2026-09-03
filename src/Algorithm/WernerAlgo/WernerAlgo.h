#ifndef __WERNER_ALGO_H
#define __WERNER_ALGO_H

#include "../AlgorithmBase/AlgorithmBase.h"
#include "../../Network/Graph/Graph.h"
#include "../../config.h"

#include <map>
#include <memory>
#include <vector>
#include <utility>
#include <algorithm>
#include <limits>
#include <cassert>

using namespace std;

/**
 * WernerAlgo（全時間存版）
 * - DP 表：L_all[t][a][b] = 非支配候選集合（shared_ptr<ZLabel>）
 * - ZLabel 內含回溯指標（left/right/prev）可重建 shape
 * - 外部 API / run 流程比照 MyAlgo1
 *
 * 注意：
 * 1) 這份碼假設 AlgorithmBase / Graph / Shape / DPParam / Path / SDpair / INF 存在
 * 2) 若你的名稱不同，請在此檔調整 include 與型別別名
 */
class WernerAlgo : public AlgorithmBase {
public:
    #define double long double
    WernerAlgo(const Graph& graph,
               const vector<pair<int,int>>& requests,
               const map<SDpair, vector<Path>>& paths);

    void run();

private:
    // ===== Werner DP Label =====
    enum class Op : unsigned char { LEAF = 0, CONT = 1, MERGE = 2 };
    class ZLabel {
        public:
        double B= 1000.0;
        double Z = 1000.0;   // 成本 / 目標
        int a = -1, b = -1, t = -1, k = -1; // 狀態索引與輔助
        int left_id=-1,right_id=-1,parent_id=-1;//左邊第幾個cand和右邊第幾個cand,祖先是第幾個cand
        Op op = Op::LEAF;
        // 回溯
        ZLabel(){}
        ZLabel(double _B, double _Z, Op _op, int _a, int _b, int _t, int _k, int pid = -1, int lid = -1, int rid = -1)
        : B(_B), Z(_Z), op(_op), a(_a), b(_b), t(_t), k(_k), parent_id(pid), left_id(lid), right_id(rid) {}
        // Assignment operator (optional, default is sufficient unless custom logic is needed)
    };

    struct DPParam{
        double eps_bucket,Zhat,Zmin,eta,T;
    }dpp;
    // ===== 參數 / 對偶變數（風格比照 MyAlgo1） =====
    double epsilon = 0.35;
    double obj = 0.0;
    vector<double> alpha;                 // 每個 request 的 dual
    vector<vector<double>> beta;          // beta[v][t]：節點-時間 dual
    vector<map<Shape_vector,double>> x;
    // ===== 全時間 DP 表：L_all[time][a][b] =====
    // 每個 cell 是「非支配候選集」
    using DPTable = vector<vector<vector<vector<ZLabel>>>>;
    // ===== 主流程 =====
    void variable_initialize();
    Shape_vector separation_oracle();

    // 在固定 path 上做 Werner DP，填滿 L_all（t=1..T-1）
    void run_dp_in_t(const Path& path, const DPParam& dpp, int t,
                     const vector<double>& edge_Z,
                     DPTable& dp_table);

    // ===== 基本操作（Pareto / 分桶 / 存儲 / 回溯 / 評分） =====
    void pareto_prune_byZ(vector<ZLabel>& cand);
    void bucket_by_Z(vector<ZLabel>& cand);

    Shape_vector backtrack_shape(const ZLabel& leaf, const vector<int>& path,
                                 const DPTable& dp_table);
    int split_dis(int s, int d, const WernerAlgo::ZLabel& L);
    pair<double,WernerAlgo::ZLabel> eval_best_J(
        int s, int d, int t, double alp, const DPTable& dp_table);

    // --- Oracle cache for incremental separation_oracle ---
    struct OracleCache {
        double best_score = 1e18;  // cached J / path_Pr
        double path_pr = 0;
        Shape_vector shape;
        bool valid = false;
    };
    vector<vector<OracleCache>> oracle_cache;
    // Requests with the same SD pair share path frontiers; alpha is applied
    // only when the finished frontier is scored.
    vector<vector<int>> request_groups;
    int oracle_worker_count = 1;
    vector<DPTable> dp_workspaces;
    set<int> dirty_nodes;
    set<int> dirty_alpha_idxs;  // alpha 被更新的 request indices
};

#endif // __WERNER_ALGO_H
