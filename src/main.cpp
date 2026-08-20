
#include "./config.h"
#include <sys/resource.h>
#include <array>
#include <new>
#include <stdexcept>
#include "Network/Graph/Graph.h"
#include "Algorithm/AlgorithmBase/AlgorithmBase.h"
#include "Algorithm/MyAlgo1/MyAlgo1.h"
#include "Algorithm/MyAlgo2/MyAlgo2.h"
#include "Algorithm/MyAlgo3/MyAlgo3.h"
#include "Algorithm/MyAlgo4/MyAlgo4.h"
#include "Algorithm/MyAlgo5/MyAlgo5.h"
#include "Algorithm/MyAlgo6/MyAlgo6.h"
#include "Algorithm/WernerAlgo/WernerAlgo.h"
#include "Algorithm/WernerAlgo2/WernerAlgo2.h"
#include "Algorithm/WernerAlgo3/WernerAlgo3.h"
#include "Algorithm/WernerAlgo_UB/WernerAlgo_UB.h"
#include "Algorithm/EFiRAP/EFiRAP.h"
#include "Algorithm/EFiRAP_longtime/EFiRAP_longtime.h"
#include "Network/PathMethod/PathMethodBase/PathMethod.h"
#include "Network/PathMethod/Greedy/Greedy.h"
#include "Network/PathMethod/QCAST/QCAST.h"
#include "Network/PathMethod/REPS/REPS.h"

using namespace std;

// [DEBUG] 印出當前 RSS / 系統記憶體 — 方便定位 std::bad_alloc 發生點
static void DBG_mem(const char* tag) {
    struct rusage ru;
    if(getrusage(RUSAGE_SELF, &ru) == 0) {
        // ru_maxrss 在 Linux 是 KB
        cerr << "[MEM] " << tag << " RSS=" << ru.ru_maxrss << " KB" << endl;
    } else {
        cerr << "[MEM] " << tag << " (getrusage failed)" << endl;
    }
    cerr.flush();
}

#define DBG_HERE(tag) do { cerr << "[CKPT] " << tag << endl; cerr.flush(); } while(0)

SDpair generate_new_request(int num_of_node){
    random_device rd;
    default_random_engine generator = default_random_engine(rd());
    uniform_int_distribution<int> unif(0, num_of_node-1);
    int node1 = unif(generator), node2 = unif(generator);
    while(node1 == node2) node2 = unif(generator);

    return make_pair(node1, node2);
}

vector<SDpair> generate_requests(Graph &graph, int requests_cnt, int length_lower, int length_upper) {
    int n = graph.get_num_nodes();
    vector<SDpair> cand;
    random_device rd;
    default_random_engine generator = default_random_engine(rd());
    uniform_int_distribution<int> repeat_dist(3, 6);
    for(int i = 0; i < n; i++) {
        for(int j = 0; j < n; j++) {
            if(i == j) continue;
            int dist = graph.distance(i, j);
            if(dist >= length_lower && dist <= length_upper) {
                cand.emplace_back(i, j);
            }
        }
    }

    if(cand.empty()) {
        cerr << "[generate_requests] no SD pairs satisfy hop range ["
             << length_lower << ", " << length_upper << "]" << endl;
        return {};
    }

    vector<SDpair> requests;
    while((int)requests.size() < requests_cnt) {
        shuffle(cand.begin(), cand.end(), generator);
        for(const SDpair& sdpair : cand) {
            int repeat = min(repeat_dist(generator),
                             requests_cnt - (int)requests.size());
            while(repeat-- > 0) requests.push_back(sdpair);
            if((int)requests.size() == requests_cnt) break;
        }
    }

    shuffle(requests.begin(), requests.end(), generator);
    return requests;
}
vector<SDpair> generate_requests_fid(Graph &graph, int requests_cnt,double fid_th,double hop_th, double fid_upper = 1) {
    int n = graph.get_num_nodes();
    vector<pair<SDpair,double>> cand[22];
    random_device rd;
    default_random_engine generator = default_random_engine(rd());
    uniform_int_distribution<int> unif(0, 1e9);
    int sd_cnt=0;
    for(int i = 0; i < n; i++) {
        for(int j = 0; j < n; j++) {
            if(i == j) continue;
            double fid = graph.get_ini_fid(i,j);
            //cerr<<"fid of "<<i<<" "<<j<<" : "<<fid<<endl;
            assert(fid>=0.0&&fid<=1.0);
            if(fid > fid_th && fid <= fid_upper && graph.distance(i,j)>=hop_th) {
                int index = fid/0.05;
                //index-=5;
                if(index < 0) continue;
                if(index > 20) index = 20;
                int d=graph.distance(i, j),f0=fid,prob=pow(0.1,d)*pow(0.9,max(d-1,0));
                //double score = f0+prob*100-0.1*d;
                cand[index].emplace_back(std::make_pair(std::make_pair(i, j), graph.distance(i, j)));
                if(graph.distance(i,j)>=1)sd_cnt++;
            }
        }
    }
     cerr << "\033[1;32m"<< "[SD ini pairs] : "<<sd_cnt<< "\033[0m"<< endl;
    /*for(int i=21;i>=0;i--){
        if(!cand[i].empty()){
            random_shuffle(cand[i].begin(), cand[i].end());
        }
    } */
    /* for(int i=21;i>=0;i--){
        sort(cand[i].begin(),cand[i].end(),[](const pair<SDpair,double>& L,const pair<SDpair,double>& R){
            return L.second > R.second;
        }) ;
    }  */
    for(int i=0;i<22;i++){
        random_shuffle(cand[i].begin(), cand[i].end());
    }
    // 檢查是否有任何候選
    bool any_cand = false;
    for (int i = 0; i < 22; i++) if (!cand[i].empty()) any_cand = true;
    if (!any_cand) {
        cerr << "[generate_requests_fid] WARNING: no candidates found (fid_th=" << fid_th << ", hop_th=" << hop_th << ")" << endl;
        return {};
    }

    vector<SDpair> requests;
    int pos[22];
    for(int i=0;i<22;i++) pos[i]=0;
    int idx=0;
    while((int)requests.size()<requests_cnt){
        int cnt=unif(generator) % 4 +3;
        cnt=min(cnt,(int)(requests_cnt-(int)requests.size()));
        // 找下一個非空桶（有保護）
        int tries = 0;
        while(cand[21-idx].empty()){
            idx++;
            if(idx>=22) idx=0;
            if(++tries > 22) break;  // 防止無限迴圈
        }
        if(tries > 22) break;
        if(!cand[21-idx].empty()){
            for(int i=0;i<cnt;i++){
                requests.push_back(cand[21-idx][pos[21-idx]].first);
            }
            pos[21-idx]++;
            pos[21-idx]%=cand[21-idx].size();
        }
        idx=(idx+1)%22;
    }
    if ((int)requests.size() < requests_cnt)
        cerr << "[generate_requests_fid] only generated " << requests.size() << "/" << requests_cnt << " requests" << endl;
    return requests;
}
// 生成「purification 能帶來優勢」的 request
// 用 Shape::get_fidelity 精確計算（和 rounding 階段的 check_resource 完全一致）
// 構造一個最簡單的 balanced-tree shape，分別算有/無 purify 的 real fidelity
vector<SDpair> generate_requests_purify_needed(Graph &graph, int requests_cnt, int min_hop = 2) {
    int n = graph.get_num_nodes();
    double fid_th = graph.get_fidelity_threshold();
    double A = graph.get_A(), B = graph.get_B();
    double n_param = graph.get_n(), T = graph.get_T(), tao = graph.get_tao();
    const auto& F_init = graph.get_F_init();

    // BFS 找最短路徑
    auto bfs_path = [&](int src, int dst) -> vector<int> {
        vector<int> parent(n, -1);
        vector<bool> vis(n, false);
        queue<int> que;
        vis[src] = true;
        que.push(src);
        while (!que.empty()) {
            int u = que.front(); que.pop();
            if (u == dst) break;
            for (int v : graph.adj_list[u]) {
                if (!vis[v]) {
                    vis[v] = true;
                    parent[v] = u;
                    que.push(v);
                }
            }
        }
        if (!vis[dst]) return {};
        vector<int> path;
        for (int v = dst; v != -1; v = parent[v]) path.push_back(v);
        reverse(path.begin(), path.end());
        return path;
    };

    // 用和 Shape::get_fidelity 完全相同的公式直接計算 path fidelity
    // F = A + B*exp(-(t/T)^n), decoherence 用 pass_tao
    auto t2F = [&](double t) -> double {
        if(t >= 1e5) return 0;
        return A + B * exp(-pow(t / T, n_param));
    };
    auto F2t = [&](double F) -> double {
        if(F <= A + 1e-9) return 1e9;
        return T * pow(-log((F - A) / B), 1.0 / n_param);
    };
    auto pass_tao_f = [&](double F) -> double {
        return t2F(F2t(F) + tao);
    };
    auto Fswap = [&](double Fa, double Fb) -> double {
        if(Fa <= A + 1e-9 || Fb <= A + 1e-9) return 0;
        return Fa * Fb + (1.0 / 3.0) * (1.0 - Fa) * (1.0 - Fb);
    };

    // 遞迴計算 balanced-tree schedule 的 end-to-end fidelity
    // edges[i] = F_init of edge i, purify_rounds[i] = rounds for edge i (0=none)
    function<double(int, int, const vector<double>&, const vector<int>&)> calc_fidelity;
    calc_fidelity = [&](int left, int right, const vector<double>& edge_fids, const vector<int>& pur_rounds) -> double {
        if (left == right - 1) {
            // Leaf: single edge
            double raw_f = edge_fids[left];
            int rounds = pur_rounds[left];
            if (rounds > 0) {
                double Fbase = raw_f;
                double Fcur = raw_f;
                for (int r = 0; r < rounds; r++) {
                    // Paper Eq.(4): pumping purification with a fresh pair Fbase.
                    double Fcur_bar = 1.0 - Fcur;
                    double Fbase_bar = 1.0 - Fbase;
                    double den = Fcur * Fbase
                               + (1.0 / 3.0) * Fcur * Fbase_bar
                               + (1.0 / 3.0) * Fcur_bar * Fbase
                               + (5.0 / 9.0) * Fcur_bar * Fbase_bar;
                    double num = Fcur * Fbase
                               + (1.0 / 9.0) * Fcur_bar * Fbase_bar;
                    Fcur = num / den;
                }
                return pass_tao_f(Fcur);
            }
            return pass_tao_f(raw_f);
        }
        // Balanced split
        int mid = (left + right) / 2;
        double Fa = calc_fidelity(left, mid, edge_fids, pur_rounds);
        double Fb = calc_fidelity(mid, right, edge_fids, pur_rounds);
        // Swap + 1 tao decoherence
        return t2F(F2t(Fswap(pass_tao_f(Fa), pass_tao_f(Fb))) + (tao - tao));
        // 注意: 簡化模型，假設 swap 後不額外等待（pass_time - tao = 0）
    };

    const double margin_ratio = 1.05;  // fidelity 超過 threshold 但不超過 5% 算「邊緣」
    const int max_purify_rounds = 3;

    vector<pair<double, SDpair>> candidates;

    struct HopDiag { int total=0, pass_no=0, marginal=0, sweet=0, fail_both=0; };
    map<int, HopDiag> diag;

    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            vector<int> path = bfs_path(i, j);
            if (path.empty()) continue;
            int h = (int)path.size() - 1;
            if (h < min_hop) continue;

            // 收集每條 edge 的 F_init
            vector<double> edge_fids(h);
            for (int k = 0; k < h; k++)
                edge_fids[k] = graph.get_F_init(path[k], path[k+1]);

            // 不做 purify 的真實 fidelity（用和 Shape::get_fidelity 相同的公式）
            vector<int> no_pur(h, 0);
            double fid_no = calc_fidelity(0, h, edge_fids, no_pur);

            // 嘗試 1~3 rounds purification
            int best_rounds = -1;
            double fid_pur = 0;
            for (int rr = 1; rr <= max_purify_rounds; rr++) {
                vector<int> pur(h, rr);
                double f = calc_fidelity(0, h, edge_fids, pur);
                if (f >= fid_th) {
                    best_rounds = rr;
                    fid_pur = f;
                    break;
                }
            }

            diag[h].total++;

            if (fid_no < fid_th && best_rounds > 0) {
                // A: 嚴格甜蜜點 — 不做 purify 過不了，做了能過
                diag[h].sweet++;
                double score = 3.0 - best_rounds * 0.3 + fid_pur / fid_th * 0.1;
                candidates.push_back({score, {i, j}});
                candidates.push_back({score, {j, i}});
            } else if (fid_no >= fid_th && fid_no < fid_th * margin_ratio && best_rounds > 0) {
                // B: 邊緣受益者 — 不做 purify 勉強過，做 purify 後明顯更好
                diag[h].marginal++;
                double score = 1.0 + fid_pur / fid_no * 0.1;
                candidates.push_back({score, {i, j}});
                candidates.push_back({score, {j, i}});
            } else if (fid_no >= fid_th * margin_ratio) {
                diag[h].pass_no++;
            } else {
                diag[h].fail_both++;
            }
        }
    }

    // 診斷
    cerr << "\033[1;33m" << "[purify_needed] diagnostics (fid_th=" << fid_th
         << ", margin=" << margin_ratio << ", max_rounds=" << max_purify_rounds << "):" << "\033[0m" << endl;
    for (auto &[hop, stats] : diag) {
        cerr << "  hop=" << hop
             << " | pairs=" << stats.total
             << " | comfy_pass=" << stats.pass_no
             << " | marginal=" << stats.marginal
             << " | strict_sweet=" << stats.sweet
             << " | fail_both=" << stats.fail_both
             << endl;
    }
    cerr << "\033[1;33m" << "[purify_needed] total candidates=" << candidates.size()
         << " (strict + marginal)" << "\033[0m" << endl;

    if (candidates.empty()) {
        cerr << "\033[1;31m" << "[purify_needed] WARNING: no pairs found! "
             << "Consider lowering min_fidelity or raising fidelity_threshold or min_hop."
             << "\033[0m" << endl;
        return {};
    }

    // 按 hop 數分桶，每桶內 shuffle，然後 round-robin 均勻抽取
    map<int, vector<SDpair>> hop_buckets;
    for (auto &[score, sd] : candidates) {
        vector<int> p = bfs_path(sd.first, sd.second);
        int h = p.empty() ? 0 : (int)p.size() - 1;
        hop_buckets[h].push_back(sd);
    }

    random_device rd;
    default_random_engine gen(rd());

    // 每桶 shuffle
    vector<pair<int, vector<SDpair>>> buckets_vec;
    for (auto &[h, vec] : hop_buckets) {
        shuffle(vec.begin(), vec.end(), gen);
        buckets_vec.push_back({h, vec});
    }

    cerr << "\033[1;33m" << "[purify_needed] hop distribution:";
    for (auto &[h, vec] : buckets_vec)
        cerr << " hop" << h << "=" << vec.size();
    cerr << "\033[0m" << endl;

    // Round-robin：輪流從每個 hop 桶取，每次取 2~4 個同 SD pair
    vector<SDpair> requests;
    vector<int> pos(buckets_vec.size(), 0);
    uniform_int_distribution<int> rep_dist(2, 4);
    int bucket_idx = 0;
    while ((int)requests.size() < requests_cnt) {
        // 找到一個還有 pair 的桶
        bool found = false;
        for (int try_cnt = 0; try_cnt < (int)buckets_vec.size(); try_cnt++) {
            int bi = (bucket_idx + try_cnt) % (int)buckets_vec.size();
            if (pos[bi] < (int)buckets_vec[bi].second.size()) {
                int rep = min(rep_dist(gen), requests_cnt - (int)requests.size());
                for (int r = 0; r < rep; r++)
                    requests.push_back(buckets_vec[bi].second[pos[bi]]);
                pos[bi]++;
                bucket_idx = (bi + 1) % (int)buckets_vec.size();
                found = true;
                break;
            }
        }
        if (!found) {
            // 所有桶都用完，循環重來
            for (int i = 0; i < (int)pos.size(); i++) pos[i] = 0;
            for (auto &[h, vec] : buckets_vec) shuffle(vec.begin(), vec.end(), gen);
        }
    }
    requests.resize(requests_cnt);

    shuffle(requests.begin(), requests.end(), gen);
    return requests;
}

// Build the default workload from topology and physical feasibility, never
// from an algorithm's output.  "common" requests already meet the threshold
// without purification and are split into lower- and higher-fidelity strata.
// "purification" requests miss the threshold without purification but meet it
// after at most three pumping rounds per link.
//
// To exercise node-time packing decisions, a controlled fraction of every
// stratum is sampled from SD pairs whose shortest paths traverse the busiest
// intermediate nodes in the common-feasible candidate pool.  The remaining
// requests are sampled from the background pool.  This creates moderate,
// topology-dependent resource pressure while keeping one shared, algorithm-
// independent workload and preserving heterogeneous fidelity values.
vector<SDpair> generate_stratified_requests(
    Graph &graph,
    int requests_cnt,
    double purification_fraction = 0.25,
    double hotspot_fraction = 0.65,
    int minimum_hops = 2,
    int maximum_hops = 4,
    int hotspot_candidate_limit = 3) {
    const int node_count = graph.get_num_nodes();
    const double threshold = graph.get_fidelity_threshold();
    const double A = graph.get_A(), B = graph.get_B();
    const double n_param = graph.get_n();
    const double T = graph.get_T(), tao = graph.get_tao();
    const int max_purification_rounds = 3;

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

    auto t2F = [&](double time) -> double {
        if(time >= 1e5) return 0.0;
        return A + B * exp(-pow(time / T, n_param));
    };
    auto F2t = [&](double fidelity) -> double {
        if(fidelity <= A + 1e-9) return 1e9;
        return T * pow(-log((fidelity - A) / B), 1.0 / n_param);
    };
    auto pass_tao = [&](double fidelity) -> double {
        return t2F(F2t(fidelity) + tao);
    };
    auto swap_fidelity = [&](double left, double right) -> double {
        if(left <= A + 1e-9 || right <= A + 1e-9) return 0.0;
        return left * right
             + (1.0 / 3.0) * (1.0 - left) * (1.0 - right);
    };

    function<double(int, int, const vector<double>&, const vector<int>&)>
        balanced_fidelity;
    balanced_fidelity = [&] (
        int left,
        int right,
        const vector<double>& link_fidelity,
        const vector<int>& purification_rounds) -> double {
        if(right == left + 1) {
            double raw = link_fidelity[left];
            double purified = raw;
            for(int round = 0; round < purification_rounds[left]; ++round) {
                const double current_bar = 1.0 - purified;
                const double raw_bar = 1.0 - raw;
                const double denominator =
                    purified * raw
                    + (1.0 / 3.0) * purified * raw_bar
                    + (1.0 / 3.0) * current_bar * raw
                    + (5.0 / 9.0) * current_bar * raw_bar;
                const double numerator =
                    purified * raw + (1.0 / 9.0) * current_bar * raw_bar;
                purified = numerator / denominator;
            }
            return pass_tao(purified);
        }

        const int middle = (left + right) / 2;
        const double left_fidelity = balanced_fidelity(
            left, middle, link_fidelity, purification_rounds);
        const double right_fidelity = balanced_fidelity(
            middle, right, link_fidelity, purification_rounds);
        return swap_fidelity(
            pass_tao(left_fidelity), pass_tao(right_fidelity));
    };

    struct CandidateProfile {
        SDpair request;
        Path path;
        vector<int> feasible_two_hop_intermediates;
        double base_fidelity;
        bool common_feasible;
    };
    struct ScoredRequest {
        SDpair request;
        double base_fidelity;
        int pressure;
        bool crosses_hotspot;
    };

    minimum_hops = max(1, minimum_hops);
    maximum_hops = max(minimum_hops, maximum_hops);
    vector<CandidateProfile> candidate_profiles;
    vector<int> common_transit_load(node_count, 0);
    vector<int> all_transit_load(node_count, 0);
    map<int, array<int, 3>> diagnostics;

    for(int source = 0; source < node_count; ++source) {
        for(int destination = source + 1; destination < node_count;
            ++destination) {
            Path path = shortest_path(source, destination);
            const int hops = (int)path.size() - 1;
            if(hops < minimum_hops || hops > maximum_hops) continue;

            vector<double> link_fidelity(hops);
            for(int link = 0; link < hops; ++link) {
                link_fidelity[link] = graph.get_F_init(
                    path[link], path[link + 1]);
            }

            vector<int> no_purification(hops, 0);
            const double base_fidelity = balanced_fidelity(
                0, hops, link_fidelity, no_purification);
            diagnostics[hops][0]++;

            // Enumerate every threshold-feasible two-hop route.  A hotspot
            // request is useful for comparing path/numerology algorithms only
            // when the same SD pair also has a feasible route around the
            // hotspot; considering just one BFS path hides that trade-off.
            vector<int> feasible_two_hop_intermediates;
            Path best_common_path;
            double best_common_fidelity = -1.0;
            if(minimum_hops <= 2 && maximum_hops >= 2) {
                for(int intermediate : graph.adj_list[source]) {
                    if(!graph.adj_set[intermediate].count(destination))
                        continue;
                    vector<double> two_hop_fidelity{
                        graph.get_F_init(source, intermediate),
                        graph.get_F_init(intermediate, destination)};
                    vector<int> no_two_hop_purification(2, 0);
                    const double candidate_fidelity = balanced_fidelity(
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
                    best_common_fidelity, true});
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
                    {source, destination}, path, {}, base_fidelity, true});
                for(int position = 1;
                    position + 1 < (int)path.size(); ++position) {
                    const int intermediate = path[position];
                    common_transit_load[intermediate]++;
                    all_transit_load[intermediate]++;
                }
                diagnostics[hops][1]++;
                continue;
            }

            bool feasible_with_purification = false;
            for(int rounds = 1; rounds <= max_purification_rounds; ++rounds) {
                vector<int> purification(hops, rounds);
                if(balanced_fidelity(0, hops, link_fidelity, purification)
                   >= threshold) {
                    feasible_with_purification = true;
                    break;
                }
            }
            if(feasible_with_purification) {
                candidate_profiles.push_back({
                    {source, destination}, path, {}, base_fidelity, false});
                for(int position = 1; position + 1 < (int)path.size();
                    ++position)
                    all_transit_load[path[position]]++;
                diagnostics[hops][2]++;
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
    const int hotspot_node_limit = min(1, node_count);
    for(int position = 0; position < hotspot_node_limit; ++position) {
        const int node = node_order[position];
        if(hotspot_load[node] <= 0) break;
        hotspot_nodes.insert(node);
    }

    vector<ScoredRequest> common_candidates_by_fidelity;
    vector<ScoredRequest> purification_candidates;
    for(const CandidateProfile& profile : candidate_profiles) {
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
                    && (!profile.common_feasible
                        || has_background_alternative)};
            if(profile.common_feasible)
                common_candidates_by_fidelity.push_back(candidate);
            else
                purification_candidates.push_back(candidate);
        };
        add_orientation(profile.request);
        add_orientation({profile.request.second, profile.request.first});
    }

    // Keep the lower and upper fidelity tails.  The omitted middle candidates
    // would make resource-aware and fidelity-aware choices nearly
    // indistinguishable; the two tails create the intended quality/resource
    // trade-off without relying on a hand-tuned absolute fidelity boundary.
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
    vector<ScoredRequest> marginal_common_candidates;
    vector<ScoredRequest> high_common_candidates;
    const int common_split =
        (int)common_candidates_by_fidelity.size() / 2;
    const int fidelity_tail_size = common_candidates_by_fidelity.empty()
        ? 0
        : max(1, (int)ceil(common_candidates_by_fidelity.size() * 0.35));
    for(int i = 0; i < fidelity_tail_size; ++i) {
        marginal_common_candidates.push_back(
            common_candidates_by_fidelity[i]);
        high_common_candidates.push_back(
            common_candidates_by_fidelity[
                common_candidates_by_fidelity.size() - 1 - i]);
    }

    if(purification_fraction < 0.0) purification_fraction = 0.0;
    if(purification_fraction > 1.0) purification_fraction = 1.0;
    if(hotspot_fraction < 0.0) hotspot_fraction = 0.0;
    if(hotspot_fraction > 1.0) hotspot_fraction = 1.0;
    int purification_target = (int)lround(
        requests_cnt * purification_fraction);
    int common_target = requests_cnt - purification_target;

    if(common_candidates_by_fidelity.empty()) {
        cerr << "[request_mix] WARNING: common-feasible pool is empty" << endl;
        purification_target = requests_cnt;
        common_target = 0;
    }
    if(purification_candidates.empty()) {
        cerr << "[request_mix] WARNING: purification-feasible pool is empty"
             << endl;
        common_target = requests_cnt;
        purification_target = 0;
    }
    if(common_candidates_by_fidelity.empty()
       && purification_candidates.empty()) {
        cerr << "[request_mix] ERROR: no physically feasible 1--2-hop requests"
             << endl;
        return {};
    }
    int high_common_target = common_target / 2;
    int marginal_common_target = common_target - high_common_target;
    if(marginal_common_candidates.empty()) {
        high_common_target = common_target;
        marginal_common_target = 0;
    }
    if(high_common_candidates.empty()) {
        marginal_common_target = common_target;
        high_common_target = 0;
    }
    const double effective_purification_fraction = requests_cnt > 0
        ? (double)purification_target / requests_cnt
        : 0.0;
    const double effective_high_common_fraction = requests_cnt > 0
        ? (double)high_common_target / requests_cnt
        : 0.0;

    random_device random_source;
    default_random_engine generator(random_source());
    uniform_int_distribution<int> repeat_distribution(2, 4);
    auto append_pressure_mixed = [&] (
        vector<ScoredRequest> candidates,
        int target,
        double target_hotspot_fraction,
        vector<SDpair>& output) {
        if(target <= 0) return;

        vector<SDpair> hotspot_candidates;
        vector<SDpair> background_candidates;
        for(const ScoredRequest& candidate : candidates) {
            if(candidate.crosses_hotspot)
                hotspot_candidates.push_back(candidate.request);
            else
                background_candidates.push_back(candidate.request);
        }

        // Model a burst of a few elephant SD flows through the bottleneck.
        // Repeating these flows before and after the path builder exhausts the
        // hotspot gives the same request alternative congested/background
        // paths, which is essential for exercising joint path/numerology
        // selection rather than only admission control.
        hotspot_candidate_limit = max(1, hotspot_candidate_limit);
        if((int)hotspot_candidates.size() > hotspot_candidate_limit)
            hotspot_candidates.resize(hotspot_candidate_limit);

        int hotspot_target = (int)lround(
            target * target_hotspot_fraction);
        if(hotspot_candidates.empty()) hotspot_target = 0;
        if(background_candidates.empty()) hotspot_target = target;
        const int background_target = target - hotspot_target;

        auto sample_repeated = [&](vector<SDpair> pool, int amount) {
            vector<SDpair> sampled;
            sampled.reserve(amount);
            while((int)sampled.size() < amount) {
                shuffle(pool.begin(), pool.end(), generator);
                for(const SDpair& request : pool) {
                    int repeat = min(
                        repeat_distribution(generator),
                        amount - (int)sampled.size());
                    for(int copy = 0; copy < repeat; ++copy)
                        sampled.push_back(request);
                    if((int)sampled.size() == amount) break;
                }
            }
            return sampled;
        };

        vector<SDpair> hotspot_requests =
            sample_repeated(hotspot_candidates, hotspot_target);
        vector<SDpair> background_requests =
            sample_repeated(background_candidates, background_target);

        int hotspot_position = 0;
        int background_position = 0;
        const double effective_hotspot_fraction = target > 0
            ? (double)hotspot_target / target
            : 0.0;
        for(int prefix = 1; prefix <= target; ++prefix) {
            const int desired_hotspot = (int)lround(
                prefix * effective_hotspot_fraction);
            if(hotspot_position < desired_hotspot)
                output.push_back(hotspot_requests[hotspot_position++]);
            else
                output.push_back(
                    background_requests[background_position++]);
        }
    };

    vector<SDpair> marginal_common_requests;
    vector<SDpair> high_common_requests;
    vector<SDpair> purification_requests;
    marginal_common_requests.reserve(marginal_common_target);
    high_common_requests.reserve(high_common_target);
    purification_requests.reserve(purification_target);
    // Marginal requests include more resource-efficient background traffic,
    // whereas high-quality and purification-needed requests apply stronger
    // pressure to the hotspot.  This produces a controlled value/resource
    // conflict instead of uniformly congesting every fidelity class.
    const double marginal_hotspot_fraction = max(
        0.0L, hotspot_fraction - 0.25L);
    append_pressure_mixed(
        marginal_common_candidates,
        marginal_common_target,
        marginal_hotspot_fraction,
        marginal_common_requests);
    append_pressure_mixed(
        high_common_candidates,
        high_common_target,
        hotspot_fraction,
        high_common_requests);
    append_pressure_mixed(
        purification_candidates, purification_target, hotspot_fraction,
        purification_requests);

    // The request-count experiment consumes prefixes of this pool.  Mix in
    // ten-request blocks so every 80/100/... prefix stays close to the target
    // composition instead of depending on one global shuffle.
    vector<SDpair> requests;
    requests.reserve(requests_cnt);
    int marginal_common_position = 0;
    int high_common_position = 0;
    int purification_position = 0;
    while((int)requests.size() < requests_cnt) {
        const int block_size = min(10, requests_cnt - (int)requests.size());
        const int prefix_end = (int)requests.size() + block_size;
        const int desired_purification = (int)lround(
            prefix_end * effective_purification_fraction);
        int block_purification =
            desired_purification - purification_position;
        block_purification = min(
            block_purification,
            (int)purification_requests.size() - purification_position);
        const int desired_high_common = (int)lround(
            prefix_end * effective_high_common_fraction);
        int block_high_common =
            desired_high_common - high_common_position;
        block_high_common = min(
            block_high_common,
            (int)high_common_requests.size() - high_common_position);
        const int block_marginal_common =
            block_size - block_purification - block_high_common;

        vector<SDpair> block;
        block.reserve(block_size);
        for(int i = 0; i < block_marginal_common; ++i)
            block.push_back(
                marginal_common_requests[marginal_common_position++]);
        for(int i = 0; i < block_high_common; ++i)
            block.push_back(high_common_requests[high_common_position++]);
        for(int i = 0; i < block_purification; ++i)
            block.push_back(
                purification_requests[purification_position++]);
        shuffle(block.begin(), block.end(), generator);
        requests.insert(requests.end(), block.begin(), block.end());
    }

    cerr << "[request_mix] threshold=" << threshold
         << " T=" << T << " tao=" << tao
         << " hops=" << minimum_hops << "--" << maximum_hops
         << " | selected common=" << common_target
         << " (marginal=" << marginal_common_target
         << ", high=" << high_common_target << ")"
         << " purification=" << purification_target << endl;
    if(!common_candidates_by_fidelity.empty()) {
        const double minimum_common_fidelity =
            common_candidates_by_fidelity.front().base_fidelity;
        const double median_common_fidelity =
            common_candidates_by_fidelity[common_split].base_fidelity;
        const double maximum_common_fidelity =
            common_candidates_by_fidelity.back().base_fidelity;
        cerr << "  common fidelity range=" << minimum_common_fidelity
             << " ... " << median_common_fidelity
             << " ... " << maximum_common_fidelity << endl;
    }
    cerr << "  hotspot nodes:";
    for(int node : hotspot_nodes)
        cerr << " v" << node << "(candidate_load=" << hotspot_load[node]
             << ")";
    cerr << " | hotspot fractions marginal/high="
         << marginal_hotspot_fraction << "/" << hotspot_fraction << endl;

    int hotspot_request_count = 0;
    int peak_internal_node_load = 0;
    vector<int> selected_internal_load(node_count, 0);
    set<SDpair> unique_requests;
    for(const SDpair& request : requests) {
        unique_requests.insert(request);
        Path path = shortest_path(request.first, request.second);
        bool crosses_hotspot = false;
        for(int position = 1; position + 1 < (int)path.size(); ++position) {
            const int node = path[position];
            crosses_hotspot = crosses_hotspot || hotspot_nodes.count(node);
            peak_internal_node_load = max(
                peak_internal_node_load, ++selected_internal_load[node]);
        }
        hotspot_request_count += crosses_hotspot;
    }
    cerr << "  selected hotspot requests=" << hotspot_request_count << "/"
         << requests.size() << " | unique directed SD pairs="
         << unique_requests.size() << " | peak internal-node demand="
         << peak_internal_node_load << endl;
    for(const auto& [hops, count] : diagnostics) {
        cerr << "  hop=" << hops
             << " candidates=" << count[0]
             << " common=" << count[1]
             << " purification=" << count[2] << endl;
    }
    return requests;
}

int main(){
    string file_path = "../data/";

    map<string, double> default_setting;
    default_setting["num_nodes"] = 100;
    default_setting["request_cnt"] = 80;
    default_setting["entangle_lambda"] = 0.045;
    default_setting["time_limit"] = 13;
    // A memory budget of 10 creates measurable contention without making the
    // experiment overwhelmingly capacity-limited.
    default_setting["avg_memory"] = 10;
    default_setting["tao"] = 0.002;
    default_setting["path_length"] = 3;
    // With threshold 0.8, max_fidelity=0.99 leaves a controlled set of
    // no-purification-feasible three-hop requests.  Those longer paths expose
    // swapping-tree and time-slot allocation differences that two-hop-only
    // workloads cannot distinguish.
    default_setting["min_fidelity"] = 0.80;
    default_setting["max_fidelity"] = 0.99;
    default_setting["swap_prob"] = 0.9;
    default_setting["fidelity_threshold"] = 0.8;
    default_setting["entangle_time"] = 0.00025;
    default_setting["entangle_prob"] = 0.01;
    default_setting["Zmin"]=0.02702867239;
    default_setting["bucket_eps"]=0.01;
    default_setting["time_eta"]=0.001;
    // The default workload itself is stratified below.  hop_count is only the
    // requested distance in the dedicated hop_count experiment.
    default_setting["hop_count"]=2;
    default_setting["purification_request_fraction"]=0.25;
    default_setting["request_hotspot_fraction"]=0.65;
    default_setting["request_min_hops"]=2;
    default_setting["request_max_hops"]=4;
    default_setting["request_hotspot_candidate_limit"]=3;
    default_setting["delta_P"]=0.01;
    map<string, vector<double>> change_parameter;
    change_parameter["request_cnt"] = {80,100,120,140,160};
    change_parameter["num_nodes"] = {30, 40, 50, 60, 70};
    change_parameter["min_fidelity"] = {0.6, 0.7, 0.8, 0.9, 0.95};
    change_parameter["avg_memory"] = {4, 6, 8, 10, 12, 16, 20};
    // change_parameter["tao"] = {0.3, 0.4, 0.5, 0.6, 0.7};
    change_parameter["tao"] = {0.001,0.002,0.003,0.004,0.005};
    change_parameter["path_length"] = {3, 6, 9, 12, 15};
    change_parameter["swap_prob"] = {0.6, 0.7, 0.8, 0.9,0.95};
    change_parameter["fidelity_threshold"] = {0.5, 0.55, 0.6, 0.65, 0.7, 0.75, 0.8, 0.85,0.9,0.95};
    change_parameter["time_limit"] = {5,7, 9, 11, 13, 15,17,19};
    change_parameter["entangle_lambda"] = {0.0125, 0.025, 0.035, 0.045, 0.055, 0.065};
    change_parameter["entangle_time"] = {0.0001, 0.00025, 0.0004, 0.00055, 0.0007,0.00085,0.001};
    change_parameter["entangle_prob"] = {0.0001, 0.001, 0.01, 0.1, 1};
    change_parameter["hop_count"] = {1,2,3,4,5,6};
    //change_parameter["Zmin"]={0.028,0.150,0.272,0.394,0.518};
    change_parameter["bucket_eps"]={0.00001,0.0001,0.001,0.01,0.1};
    change_parameter["time_eta"]={0.00001,0.0001,0.001,0.01,0.1};
    int round = 5;
    vector<vector<SDpair>> default_requests(round);
    #pragma omp parallel for
    for(int r = 0; r < round; r++) {
        int num_nodes = default_setting["num_nodes"];
        int avg_memory = default_setting["avg_memory"];
        // int request_cnt = default_setting["request_cnt"];
        int time_limit = default_setting["time_limit"];
        double min_fidelity = default_setting["min_fidelity"];
        double max_fidelity = default_setting["max_fidelity"];
        double Zmin=default_setting["Zmin"];
        double bucket_eps=default_setting["bucket_eps"];
        double time_eta=default_setting["time_eta"];
        double swap_prob = default_setting["swap_prob"];
        double fidelity_threshold = default_setting["fidelity_threshold"];
        map<string, double> input_parameter = default_setting;
        vector<map<string, map<string, double>>> result(round);
        // double entangle_lambda = input_parameter["entangle_lambda"];
        // double entangle_time = input_parameter["entangle_time"];
        double entangle_prob = input_parameter["entangle_prob"];
        string filename = file_path + "input/round_" + to_string(r) + ".input";
        string command = "python3 graph_generator.py ";
        double A = 0.25, B = 0.75, tao = default_setting["tao"], T = 0.04, n = 2;
        // derandom
        string parameter = to_string(num_nodes);
        cerr << (command + filename + " " + parameter) << endl;
        if(system((command + filename + " " + parameter).c_str()) != 0){
            cerr<<"error:\tsystem proccess python error"<<endl;
            exit(1);
        }
        Graph graph(filename, time_limit, swap_prob, avg_memory, min_fidelity, max_fidelity, fidelity_threshold, A, B, n, T, tao,Zmin,bucket_eps,time_eta,input_parameter["delta_P"],input_parameter["entangle_lambda"],input_parameter["entangle_time"]);
        const int total_cnt = 200;  // pool must cover max(request_cnt)=160
        default_requests[r] = generate_stratified_requests(
            graph, total_cnt,
            default_setting["purification_request_fraction"],
            default_setting["request_hotspot_fraction"],
            (int)default_setting["request_min_hops"],
            (int)default_setting["request_max_hops"],
            (int)default_setting["request_hotspot_candidate_limit"]);

        {
            map<int, int> hop_dist;
            for (auto &sd : default_requests[r]) {
                int d = graph.distance(sd.first, sd.second);
                hop_dist[d]++;
            }
            cerr << "\033[1;36m"
                 << "========== Request Generation Done ==========" << endl
                 << "  total=" << default_requests[r].size()
                 << " | physically stratified SD sampling" << endl
                 << "  hop distribution: ";
            for (auto &[h, cnt] : hop_dist)
                cerr << h << "hop=" << cnt << " ";
            cerr << endl
                  << "  target purification-needed fraction="
                 << default_setting["purification_request_fraction"]
                 << " | target hotspot fraction="
                 << default_setting["request_hotspot_fraction"] << endl
                 << "================================================"
                 << "\033[0m" << endl;
        }
        assert(!default_requests[r].empty());
    }




    // vector<string> X_names = {"time_limit", "request_cnt", "num_nodes", "avg_memory", "tao"};
    //vector<string> X_names = {"request_cnt"};
    vector<string> X_names = { "request_cnt", "time_limit", "tao",  "fidelity_threshold" , "avg_memory","hop_count","swap_prob" };
    //vector<string> X_names = {"Zmin","bucket_eps","time_eta"};
    vector<string> Y_names = {"fidelity_gain", "succ_request_cnt","actual_req_cnt"};
    vector<string> algo_names = {"ZFA_UB", "ZFA", "ZFA2", "MyAlgo1", "MyAlgo3"};
    if(EFiRAP::gurobi_available()) {
        algo_names.push_back("EFiRAP");
        algo_names.push_back("EFiRAP_longtime");
    } else {
        cerr << "[EFiRAP] Gurobi support is not enabled; skipping EFiRAP "
             << "and EFiRAP_longtime."
             << endl;
    }
    // init result


    vector<PathMethod*> path_methods;
    path_methods.emplace_back(new Greedy());
    /* path_methods.emplace_back(new QCAST());
    path_methods.emplace_back(new REPS()); */
    for(PathMethod *path_method : path_methods) {

        for(string X_name : X_names) {
            for(string Y_name : Y_names){
                if(path_method->get_name() != "Greedy" && X_name != "request_cnt")
                    continue; 
                string filename = "ans/" + path_method->get_name() + "_" + X_name + "_" + Y_name + ".ans";
                fstream file( file_path + filename, ios::out );
            }
        }

        for(string X_name : X_names) {
            if(path_method->get_name() != "Greedy" && X_name != "request_cnt")
                continue; 
                
            map<string, double> input_parameter = default_setting;

            for(double change_value : change_parameter[X_name]) {
                vector<map<string, map<string, double>>> result(round);
                input_parameter[X_name] = change_value;

                // int num_nodes = input_parameter["num_nodes"];
                int avg_memory = input_parameter["avg_memory"];
                int request_cnt = input_parameter["request_cnt"];
                int time_limit = input_parameter["time_limit"];
                double min_fidelity = input_parameter["min_fidelity"];
                double max_fidelity = input_parameter["max_fidelity"];
                double Zmin = input_parameter["Zmin"];
                double bucket_eps=input_parameter["bucket_eps"];
                double time_eta=input_parameter["time_eta"];
                // double entangle_lambda = input_parameter["entangle_lambda"];
                // double entangle_time = input_parameter["entangle_time"];
                double entangle_prob = input_parameter["entangle_prob"];
                double swap_prob = input_parameter["swap_prob"];
                double fidelity_threshold = input_parameter["fidelity_threshold"];
                int hop_count = input_parameter["hop_count"];
                // int length_upper, length_lower;
                // if(input_parameter["path_length"] == -1) {
                //     length_upper = num_nodes;
                //     length_lower = 6;
                // } else {
                //     length_upper = input_parameter["path_length"] + 1;
                //     length_lower = input_parameter["path_length"] - 1;
                // }

                int sum_has_path = 0;
                //#pragma omp parallel for
                for(int r = 0; r < round; r++) {
                  try {
                    cerr << "[CKPT] === ROUND " << r << " START | X=" << X_name << " val=" << change_value << " ===" << endl;
                    DBG_mem("round_start");
                    string filename = file_path + "input/round_" + to_string(r) + ".input";
                    ofstream ofs;
                    ofs.open(file_path + "log/" + path_method->get_name() + "_" + X_name + "_in_" + to_string(change_value) + "_Round_" + to_string(r) + ".log");

                    time_t now = time(0);
                    char* dt = ctime(&now);
                    cerr  << "時間 " << dt << endl << endl;
                    ofs << "時間 " << dt << endl << endl;




                    double A = 0.25, B = 0.75, tao = input_parameter["tao"], T = 0.04, n = 2;
                    DBG_HERE("before Graph ctor");
                    Graph graph(filename, time_limit, swap_prob, avg_memory, min_fidelity, max_fidelity, fidelity_threshold, A, B, n, T, tao,Zmin,bucket_eps,time_eta,input_parameter["delta_P"],input_parameter["entangle_lambda"],input_parameter["entangle_time"]);
                    DBG_HERE("after Graph ctor");
                    DBG_mem("after_graph");

                    ofs << "--------------- in round " << r << " -------------" <<endl;
                    vector<pair<int, int>> requests;
                    if(X_name != "hop_count"){
                        int idx=0;
                        for(int i = 0; i < request_cnt; i++) {
                            /* while(graph.get_ini_fid(default_requests[r][idx].first,default_requests[r][idx].second)<fidelity_threshold){
                                idx=(idx+1)%default_requests[r].size();
                            } */
                            requests.emplace_back(default_requests[r][idx]);
                            idx=(idx+1)%default_requests[r].size();
                        }
                        DBG_HERE("requests filled from default_requests");
                    }
                    else{
                        DBG_HERE("before exact-hop request generation");
                        requests = generate_requests(
                            graph, request_cnt, hop_count, hop_count);
                        DBG_HERE("after exact-hop request generation");
                    }
                    cerr << "[CKPT] requests.size()=" << requests.size() << endl;
                    DBG_HERE("before path_graph copy");
                    Graph path_graph = graph;
                    DBG_HERE("after path_graph copy");
                    DBG_mem("after_path_graph_copy");
                    path_graph.increase_resources(10);
                    DBG_HERE("after increase_resources");
                    PathMethod *new_path_method;
                    if(path_method->get_name() == "Greedy") new_path_method = new Greedy();
                    else if(path_method->get_name() == "QCAST") new_path_method = new QCAST();
                    else if(path_method->get_name() == "REPS") new_path_method = new REPS();
                    else {
                        cerr << "unknown path method" << endl;
                        assert(false);
                    }

                    DBG_HERE("before build_paths");
                    new_path_method->build_paths(path_graph, requests);
                    DBG_HERE("after build_paths");
                    DBG_mem("after_build_paths");
                    cout << "found path" << endl;
                    const auto& raw_paths = new_path_method->get_paths();
                    map<SDpair, set<Path>> paths_st;
                    for(const auto& [sdpair, pathss] : raw_paths) {
                        for(const Path& path : pathss) {
                            paths_st[sdpair].insert(path);
                        }
                    }

                    // Give every algorithm the same explicit two-hop
                    // alternatives.  The request generator deliberately
                    // selects hotspot SD pairs with at least one feasible
                    // background intermediate; relying only on capacity-
                    // driven Greedy repetitions can otherwise omit that route.
                    for(const SDpair& sdpair : requests) {
                        const int source = sdpair.first;
                        const int destination = sdpair.second;
                        for(int intermediate : graph.adj_list[source]) {
                            if(graph.adj_set[intermediate].count(destination))
                                paths_st[sdpair].insert(
                                    {source, intermediate, destination});
                        }
                    }

                    map<SDpair, vector<Path>> paths;
                    for(const auto& [sdpair, pathss] : paths_st) {
                        for(const Path& path : pathss) {
                            paths[sdpair].push_back(path);
                        }
                    }
                    DBG_HERE("after path_st/paths build");

                    int path_len = 0, path_cnt = 0, mx_path_len = 0;

                    int has_path = 0;
                    for(const SDpair& sdpair : requests) {
                        int mi_path_len = INF;
                        has_path += !paths[sdpair].empty();
                        for(const Path& path : paths[sdpair]) {
                            mi_path_len = min(mi_path_len, (int)path.size());
                            for(int i = 1; i < (int)path.size(); i++) {
                                assert(graph.adj_set[path[i]].count(path[i - 1]));
                            }
                        }
                        if(mi_path_len != INF) {
                            mx_path_len = max(mx_path_len, mi_path_len);
                            path_cnt++;
                            path_len += mi_path_len;
                        }
                    }

                    sum_has_path += has_path;
                    cerr << "Path method: " << path_method->get_name() << "\n";
                    cerr << "Request cnt: " << request_cnt << "\n";
                    cerr << "Has Path cnt: " << has_path << "\n";
                    cerr << "Avg path length = " << path_len / (double)path_cnt << "\n";
                    cerr << "Max path length = " << mx_path_len << "\n";
                    vector<AlgorithmBase*> algorithms;
                    //algorithms.emplace_back(new WernerAlgo_UB(graph,requests,paths));
                    DBG_HERE("before new WernerAlgo3");
                    algorithms.emplace_back(new WernerAlgo3(graph,requests,paths));  // ZFA_UB (LP upper bound with purify)
                    DBG_HERE("after new WernerAlgo3");
                    DBG_mem("after_new_WernerAlgo3");
                    DBG_HERE("before new WernerAlgo");
                    algorithms.emplace_back(new WernerAlgo(graph, requests, paths));  // ZFA
                    DBG_HERE("after new WernerAlgo");
                    {
                        DBG_HERE("before new WernerAlgo2");
                        auto* zfa2 = new WernerAlgo2(graph,requests,paths);
                        DBG_HERE("after new WernerAlgo2");
                        DBG_mem("after_new_WernerAlgo2");
                        string exp_label = X_name + "=" + to_string(change_value) + " Round=" + to_string(r);
                        zfa2->set_experiment_label(exp_label);
                        algorithms.emplace_back(zfa2);
                    }
                    if(EFiRAP::gurobi_available()) {
                        DBG_HERE("before new EFiRAP");
                        algorithms.emplace_back(
                            new EFiRAP(graph, requests, paths, 3));
                        DBG_HERE("after new EFiRAP");
                    }
                    if(X_name!="Zmin"&&X_name!="bucket_eps"&&X_name!="time_eta"){
                        DBG_HERE("before new MyAlgo1");
                        algorithms.emplace_back(new MyAlgo1(graph, requests, paths));
                        DBG_HERE("after new MyAlgo1");
                        DBG_HERE("before new MyAlgo3");
                        algorithms.emplace_back(new MyAlgo3(graph, requests, paths));
                        DBG_HERE("after new MyAlgo3");
                        DBG_mem("after_all_algos_ctor");
                    }
                    if(EFiRAP_longtime::gurobi_available()) {
                        DBG_HERE("before new EFiRAP_longtime");
                        algorithms.emplace_back(
                            new EFiRAP_longtime(graph, requests, paths, 3));
                        DBG_HERE("after new EFiRAP_longtime");
                    }


                    //#pragma omp parallel for schedule(dynamic)
                    for(int i = 0; i < (int)algorithms.size(); i++) {
                        cerr << "[CKPT] >>> RUN algo[" << i << "] = " << algorithms[i]->get_name() << endl;
                        DBG_mem("before_run");
                        try {
                            algorithms[i]->run();
                        } catch(const std::bad_alloc& e) {
                            cerr << "[FATAL] std::bad_alloc inside algo[" << i << "] = "
                                 << algorithms[i]->get_name() << " : " << e.what() << endl;
                            DBG_mem("at_bad_alloc");
                            throw;
                        } catch(const std::exception& e) {
                            cerr << "[FATAL] std::exception inside algo[" << i << "] = "
                                 << algorithms[i]->get_name() << " : " << e.what() << endl;
                            throw;
                        }
                        cerr << "[CKPT] <<< DONE algo[" << i << "] = " << algorithms[i]->get_name() << endl;
                        DBG_mem("after_run");
                    }



                    for(int i = 0; i < (int)algorithms.size(); i++) {
                        for(string Y_name : Y_names) {
                            result[r][algorithms[i]->get_name()][Y_name] = algorithms[i]->get_res(Y_name);
                        }
                    }

                    now = time(0);
                    dt = ctime(&now);
                    cerr << "時間 " << dt << endl << endl;
                    ofs << "時間 " << dt << endl << endl;
                    ofs.close();

                    for(auto &algo : algorithms){
                        delete algo;
                    }
                    algorithms.clear();
                    cerr << "[CKPT] === ROUND " << r << " END ===" << endl;
                    DBG_mem("round_end");
                  } catch(const std::bad_alloc& e) {
                      cerr << "[FATAL] std::bad_alloc in round r=" << r << " : " << e.what() << endl;
                      DBG_mem("round_bad_alloc");
                      throw;
                  } catch(const std::exception& e) {
                      cerr << "[FATAL] std::exception in round r=" << r << " : " << e.what() << endl;
                      throw;
                  }

                }

                map<string, map<string, double>> sum_res;
                // for(string algo_name : algo_names){
                //     for(int r = 0; r < round; r++){
                //         result[r][algo_name]["waiting_time"] /= result[T][algo_name]["total_request"];
                //         result[r][algo_name]["encode_ratio"] = result[T][algo_name]["encode_cnt"] / (result[T][algo_name]["encode_cnt"] + result[T][algo_name]["unencode_cnt"]);
                //         result[r][algo_name]["succ-finished_ratio"] = result[T][algo_name]["throughputs"] / result[T][algo_name]["finished_throughputs"];
                //         result[r][algo_name]["fail-finished_ratio"] = 1 - result[T][algo_name]["succ-finished_ratio"];
                //         result[r][algo_name]["path_length"] = result[T][algo_name]["path_length"] / result[T][algo_name]["finished_throughputs"];
                //         result[r][algo_name]["divide_cnt"] = result[T][algo_name]["divide_cnt"] / result[T][algo_name]["finished_throughputs"];
                //         result[r][algo_name]["use_memory_ratio"] = result[T][algo_name]["use_memory"] / result[T][algo_name]["total_memory"];
                //         result[r][algo_name]["use_channel_ratio"] = result[T][algo_name]["use_channel"] / result[T][algo_name]["total_channel"];
                //     }
                // }

                for(string Y_name : Y_names) {
                    string filename = "ans/" + path_method->get_name() + "_" + X_name + "_" + Y_name + ".ans";
                    ofstream ofs;
                    ofs.open(file_path + filename, ios::app);
                    ofs << change_value << ' ';

                    for(string algo_name : algo_names){
                        for(int r = 0; r < round; r++){
                            sum_res[algo_name][Y_name] += result[r][algo_name][Y_name];
                        }
                        ofs << sum_res[algo_name][Y_name] / round << ' ';
                    }
                    ofs << endl;
                    ofs.close();
                }
            }
        }
    }
    return 0;
}
