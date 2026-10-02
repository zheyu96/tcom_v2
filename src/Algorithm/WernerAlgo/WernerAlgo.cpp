#include "WernerAlgo.h"

WernerAlgo::WernerAlgo(const Graph& graph,const vector<pair<int,int>>& requests,const map<SDpair, vector<Path>>& paths, double epsilon, double bucket_eps): AlgorithmBase(graph, requests, paths), epsilon(epsilon), bucket_eps_override(bucket_eps)
{
    algorithm_name = "ZFA";
}

void WernerAlgo::variable_initialize() {
    // 與 MyAlgo1 類似：初始化 dual 與目標
    int m = (int)requests.size()
          + graph.get_num_nodes() * graph.get_time_limit();

    double delta = (1 + epsilon) * (1.0 / pow((1 + epsilon) * m, 1.0 / epsilon));
    obj = m * delta;

    alpha.assign(requests.size(), delta);
    x.clear();
    x.resize(requests.size());
    int V = graph.get_num_nodes();
    int T = graph.get_time_limit();
    dpp.eps_bucket = bucket_eps_override > 0.0 ?
        bucket_eps_override : graph.get_bucket_eps();
    double F_th=graph.get_fidelity_threshold();
    double w_th=(4.0*F_th-1.0)/3.0;
    dpp.Zhat = sqrt(-log(w_th))+1e-9;
    dpp.Zmin = graph.get_Zmin();
    dpp.T    = time_limit-1;
    dpp.eta  = graph.get_tao()/graph.get_T();
    double bucket_q = 1.0 + dpp.eps_bucket;
    if(bucket_q <= 1.0) bucket_q = 1.0 + 1e-12;
    dpp.invLogQ = 1.0 / log(bucket_q);
    beta.assign(V, vector<double>(T, INF));

    for (int v = 0; v < V; ++v) {
        for (int t = 0; t < T; ++t) {
            int cap = graph.get_node_memory_at(v, t);
            beta[v][t] = (cap == 0) ? INF : (delta / cap);
        }
    }

    // Initialize oracle cache
    oracle_cache.clear();
    oracle_cache.resize(requests.size());
    oracle_available.clear();
    oracle_available.resize(requests.size());
    for (int i = 0; i < (int)requests.size(); i++) {
        const size_t path_count =
            get_paths(requests[i].first, requests[i].second).size();
        oracle_cache[i].resize(path_count);
        oracle_available[i].resize(path_count);
    }
    request_groups.clear();
    map<SDpair, size_t> request_group_index;
    for(int i = 0; i < (int)requests.size(); i++) {
        auto inserted = request_group_index.emplace(
            requests[i], request_groups.size());
        if(inserted.second) request_groups.emplace_back();
        request_groups[inserted.first->second].push_back(i);
    }

    // Path topology and link parameters do not change during run(). Cache
    // them once instead of rebuilding them on every oracle invocation.
    path_metadata.clear();
    for(const vector<int>& group : request_groups) {
        if(group.empty()) continue;
        const int representative = group.front();
        const vector<Path>& cur_paths = get_paths(
            requests[representative].first,
            requests[representative].second);
        for(int path_index = 0;
            path_index < (int)cur_paths.size();
            ++path_index) {
            PathMetadata metadata;
            metadata.path = &cur_paths[path_index];
            metadata.path_index = path_index;
            metadata.request_indices = group;
            metadata.edge_Z.resize(cur_paths[path_index].size() - 1);
            for(int edge = 0;
                edge + 1 < (int)cur_paths[path_index].size();
                ++edge) {
                metadata.edge_Z[edge] = sqrt(graph.get_edge_W(
                    cur_paths[path_index][edge],
                    cur_paths[path_index][edge + 1])) + dpp.eta;
            }
            metadata.path_pr = graph.path_Pr(cur_paths[path_index]);
            path_metadata.push_back(std::move(metadata));
        }
    }
    // This DP is dominated by memory traffic and allocator contention. On
    // high-core-count machines, using every virtual core is substantially
    // slower than a small worker team (96 workers were about 30x slower than
    // 8 on the 80-request benchmark).
    constexpr int MAX_ORACLE_WORKERS = 8;
    oracle_worker_count = max(
        1, min(MAX_ORACLE_WORKERS, omp_get_max_threads()));
    dp_workspaces.clear();
    dp_workspaces.resize(oracle_worker_count);
    dirty_nodes.assign(V, 0);
    dirty_alpha_idxs.assign(requests.size(), 0);
}

Shape_vector WernerAlgo::separation_oracle(){
    double most_violate=1e9;
    Shape_vector todo_shape;

    for(auto& request_availability : oracle_available) {
        fill(request_availability.begin(), request_availability.end(), 0);
    }
    auto& available = oracle_available;

    struct PathTask {
        const PathMetadata* metadata = nullptr;
        vector<int> request_indices;
    };
    vector<PathTask> tasks;
    tasks.reserve(path_metadata.size());

    // Build one task per dirty (unique SD pair, path), not per request.
    for(const PathMetadata& metadata : path_metadata) {
        const Path& path = *metadata.path;
        bool path_dirty = false;
        for(int v : path) {
            if(dirty_nodes[v]) {
                path_dirty = true;
                break;
            }
        }

        vector<int> recompute;
        recompute.reserve(metadata.request_indices.size());
        for(int i : metadata.request_indices) {
            const auto& cache = oracle_cache[i][metadata.path_index];
            if(cache.valid && !dirty_alpha_idxs[i] && !path_dirty) {
                available[i][metadata.path_index] = 1;
            } else {
                recompute.push_back(i);
            }
        }
        if(recompute.empty()) continue;

        PathTask task;
        task.metadata = &metadata;
        task.request_indices = std::move(recompute);
        tasks.push_back(std::move(task));
    }

    const int active_worker_count = max(
        1, min(oracle_worker_count, (int)tasks.size()));
    #pragma omp parallel for schedule(dynamic, 1) if(active_worker_count > 1) \
        num_threads(active_worker_count)
    for(int task_index = 0; task_index < (int)tasks.size(); task_index++) {
        const PathTask& task = tasks[task_index];
        const PathMetadata& metadata = *task.metadata;
        const Path& path = *metadata.path;
        const int T = dpp.T + 5;
        const int n = path.size() + 5;
        DPTable& dp_table = dp_workspaces[omp_get_thread_num()];
        dp_table.resize(T);
        for(int t = 0; t < (int)dp_table.size(); t++) {
            dp_table[t].resize(n);
            for(int a = 0; a < (int)dp_table[t].size(); a++) {
                dp_table[t][a].resize(n);
                for(auto& cell : dp_table[t][a]) cell.clear();
            }
        }

        for(int t = 1; t <= dpp.T; t++)
            run_dp_in_t(path, dpp, t, metadata.edge_Z, dp_table);

        const int horizon = (int)dpp.T;
        vector<vector<double>> terminal_factors(horizon + 1);
        for(int t = 1; t <= horizon; ++t) {
            const auto& terminal_labels =
                dp_table[t][0][path.size() - 1];
            auto& factors = terminal_factors[t];
            factors.resize(terminal_labels.size());
            for(size_t label_index = 0;
                label_index < terminal_labels.size();
                ++label_index) {
                const ZLabel& label = terminal_labels[label_index];
                factors[label_index] = exp(label.Z * label.Z);
            }
        }

        struct AlphaEvaluation {
            double alpha_value = 0.0;
            double best_J = 1e18;
            bool feasible = false;
            Shape_vector shape;
        };
        vector<AlphaEvaluation> evaluations;
        evaluations.reserve(task.request_indices.size());

        for(int i : task.request_indices) {
            AlphaEvaluation* evaluation = nullptr;
            for(auto& previous : evaluations) {
                if(previous.alpha_value == alpha[i]) {
                    evaluation = &previous;
                    break;
                }
            }

            if(evaluation == nullptr) {
                AlphaEvaluation current;
                current.alpha_value = alpha[i];
                ZLabel local_best_label;
                for(int t = 1; t <= horizon; ++t) {
                    auto cur_val = eval_best_J(
                        0, path.size() - 1, t, alpha[i], dp_table,
                        terminal_factors[t]);
                    if(cur_val.first < current.best_J) {
                        current.best_J = cur_val.first;
                        local_best_label = cur_val.second;
                    }
                }
                if(current.best_J < 1e18) {
                    current.shape = backtrack_shape(
                        local_best_label, path, dp_table);
                    current.feasible = true;
                }
                evaluations.push_back(std::move(current));
                evaluation = &evaluations.back();
            }

            if(evaluation->feasible) {
                auto& cache = oracle_cache[i][metadata.path_index];
                cache.shape = evaluation->shape;
                cache.path_pr = metadata.path_pr;
                cache.best_score = evaluation->best_J / cache.path_pr;
                cache.valid = true;
                available[i][metadata.path_index] = 1;
            }
        }
    }

    // Keep the old request-major/path-minor strict-min reduction order.
    for(int i = 0; i < (int)requests.size(); i++) {
        const vector<Path>& cur_paths = get_paths(
            requests[i].first, requests[i].second);
        for(int p = 0; p < (int)cur_paths.size(); p++) {
            if(!available[i][p]) continue;
            const auto& cache = oracle_cache[i][p];
            if(cache.path_pr > 0 && cache.best_score < most_violate) {
                most_violate = cache.best_score;
                todo_shape = cache.shape;
            }
        }
    }

    fill(dirty_nodes.begin(), dirty_nodes.end(), 0);
    fill(dirty_alpha_idxs.begin(), dirty_alpha_idxs.end(), 0);
    return todo_shape;
}

/*pair<Shape_vector, double>
WernerAlgo::find_min_shape(int src, int dst, double alp) {
    const auto& paths = get_paths(src, dst);

    Shape_vector best_shape;
    double best_cost = INF;

    for (const Path& path : paths) {
        // 建立全時間 DP 表：T × n × n
        int T = graph.get_time_limit();
        int n = (int)path.size();
        if (n <= 1) continue;

        L_prev.assign(T, vector<vector<vector<shared_ptr<ZLabel>>>>(
                          n, vector<vector<shared_ptr<ZLabel>>>(n)));

        // 跑完整個時間軸 DP
        run_dp_all_t(path, dpp);

        // 從最後時間層挑 [0, n-1] 的最佳 ZLabel
        shared_ptr<ZLabel> best_leaf = nullptr;
        double best_Z = INF;

        for (int t = 1; t < T; ++t) {
            auto& cell = L_prev[t][0][n-1];
            for (auto& sp : cell) {
                if (!sp) continue;
                if (sp->Z < best_Z) { best_Z = sp->Z; best_leaf = sp; }
            }
        }

        if (best_leaf) {
            auto shape = backtrack_shape(best_leaf, path);
            double final_cost = evaluate_cost(best_Z, alp, shape);
            if (final_cost < best_cost) {
                best_cost = final_cost;
                best_shape = std::move(shape);
            }
        }
    }

    if (best_cost >= INF/2) return {{}, INF};
    return {best_shape, best_cost};
}*/

void WernerAlgo::run_dp_in_t(
    const Path& path, const DPParam& dpp, int t,
    const vector<double>& edge_Z, DPTable& dp_table) {
    const int n = (int)path.size();
    const double reject_Z_squared = dpp.Zhat * dpp.Zhat *
        (1.0 + 16.0 * numeric_limits<double>::epsilon());
    vector<double> right_Z_squared;

    // -------- t = 1..T-1 外圈時間迴圈 --------
    for(int a=0;a<n-1;a++)
        for(int b=a+1;b<n;b++){
            int s=path[a],e=path[b];
            const double source_beta = beta[s][t];
            const double destination_beta = beta[e][t];
            vector<ZLabel>& cand = dp_table[t][a][b];
            cand.clear();
            //leaf
            if(a+1==b){
                double Zleaf=edge_Z[a];
                if(Zleaf<=dpp.Zhat){
                    double Bleaf=beta[s][t-1]+beta[e][t-1]+beta[s][t]+beta[e][t];
                    ZLabel L(Bleaf,Zleaf,Op::LEAF,a,b,t,-1);
                    cand.push_back(std::move(L));
                }
            }
            //continue
            const auto& pre=dp_table[t-1][a][b];
            for(int p_id=0;p_id<pre.size();p_id++){
                double Zp=pre[p_id].Z+dpp.eta;
                if(Zp<=dpp.Zhat){
                    double Bp=pre[p_id].B+source_beta+destination_beta;
                    ZLabel L(Bp,Zp,Op::CONT,a,b,t,-1,p_id);
                    cand.push_back(std::move(L));
                }
            }
            //merge
            for(int k=a+1;k<b;k++){
                const auto& L1=dp_table[t-1][a][k];
                const auto& L2=dp_table[t-1][k][b];
                if(L1.size()==0||L2.size()==0) continue;
                right_Z_squared.resize(L2.size());
                double min_right_Z_squared =
                    numeric_limits<double>::max();
                for(size_t rid = 0; rid < L2.size(); ++rid) {
                    const double right_Z = L2[rid].Z + dpp.eta;
                    const double squared = right_Z * right_Z;
                    right_Z_squared[rid] = squared;
                    if(squared < min_right_Z_squared)
                        min_right_Z_squared = squared;
                }
                for(int lid=0;lid<L1.size();lid++) {
                    const auto& left_seg=L1[lid];
                    const double left_Z = left_seg.Z + dpp.eta;
                    const double left_Z_squared = left_Z * left_Z;
                    if(left_Z_squared > reject_Z_squared ||
                       left_Z_squared + min_right_Z_squared >
                           reject_Z_squared) {
                        continue;
                    }
                    for(int rid=0;rid<L2.size();rid++){
                        const auto& right_seg=L2[rid];
                        const double Z_squared =
                            left_Z_squared + right_Z_squared[rid];
                        if(Z_squared > reject_Z_squared) continue;
                        double Zp=sqrt(Z_squared);
                        if(Zp<=dpp.Zhat){
                            double Bp=left_seg.B+right_seg.B+
                                source_beta+destination_beta;
                            ZLabel L(Bp,Zp,Op::MERGE,a,b,t,k,-1,lid,rid);
                            cand.push_back(std::move(L));
                        }
                    }
                }
            }
            bucket_by_Z(cand);
        }
}

void WernerAlgo::pareto_prune_byZ(vector<ZLabel>& cand) {
    if (cand.empty()) return;
    sort(cand.begin(), cand.end(), [](const ZLabel& x, const ZLabel& y){
        if(x.Z!=y.Z) return x.Z < y.Z;
        return x.B<y.B;
    });
    vector<ZLabel> kept;
    kept.reserve(cand.size());
    double bestB = INF;
    for (auto& L : cand) {
        if (L.B + 1e-12 < bestB) {
            bestB = L.B;
            kept.push_back(std::move(L));
        }
    }
    cand.swap(kept);
}

void WernerAlgo::bucket_by_Z(vector<ZLabel>& cand) {
    if (cand.empty()) return;
    struct BucketEntry {
        long long key;
        size_t label_index;
    };
    vector<BucketEntry> entries;
    entries.reserve(cand.size());
    for(size_t index = 0; index < cand.size(); index++){
        const ZLabel& L = cand[index];
        long long key;
        if(L.Z<=dpp.Zmin) key=0;
        else{
            key=(long long)floor(
                log(L.Z/dpp.Zmin)*dpp.invLogQ+1e-12);
            if(key<0) key=0;
        }
        entries.push_back({key, index});
    }
    sort(entries.begin(), entries.end(),
         [](const BucketEntry& left, const BucketEntry& right) {
             if(left.key != right.key) return left.key < right.key;
             return left.label_index < right.label_index;
         });

    vector<ZLabel> bucketed;
    bucketed.reserve(entries.size());
    for(size_t begin = 0; begin < entries.size();) {
        size_t end = begin + 1;
        size_t best_index = entries[begin].label_index;
        while(end < entries.size() &&
              entries[end].key == entries[begin].key) {
            const size_t candidate_index = entries[end].label_index;
            if(cand[candidate_index].B + 1e-12 < cand[best_index].B)
                best_index = candidate_index;
            ++end;
        }
        bucketed.push_back(std::move(cand[best_index]));
        begin = end;
    }
    pareto_prune_byZ(bucketed);
    sort(bucketed.begin(), bucketed.end(), [](const ZLabel& x, const ZLabel& y){
        return x.Z < y.Z;
    });
    cand.swap(bucketed);
}

Shape_vector WernerAlgo::backtrack_shape(
    const ZLabel& leaf, const vector<int>& path, const DPTable& dp_table){
    int left_id=path[leaf.a],right_id=path[leaf.b];
    if(leaf.op==Op::LEAF){
        Shape_vector result;
        result.push_back({left_id,{{leaf.t-1,leaf.t}}});
        result.push_back({right_id,{{leaf.t-1,leaf.t}}});
        return result;
    }
    if(leaf.op==Op::CONT){
        assert(leaf.parent_id>=0&&leaf.parent_id<dp_table[leaf.t-1][leaf.a][leaf.b].size());
        const ZLabel& pre_label=dp_table[leaf.t-1][leaf.a][leaf.b][leaf.parent_id];
        Shape_vector last_time=backtrack_shape(pre_label,path,dp_table);
        auto & prel=last_time.front().second[0],&prer=last_time.back().second[0];
        assert(last_time.front().first==path[leaf.a]);
        assert(last_time.back().first==path[leaf.b]);
        assert(prel.second==leaf.t-1);
        assert(prer.second==leaf.t-1);
        prel.second++;
        prer.second++;
        return last_time;
    }
    if(leaf.op==Op::MERGE){
        Shape_vector left_result,right_result,result;
        assert(leaf.k>=0);
        int k_id=path[leaf.k];
        const ZLabel& left_leaf=dp_table[leaf.t-1][leaf.a][leaf.k][leaf.left_id];
        left_result=backtrack_shape(left_leaf,path,dp_table);
        const ZLabel& right_leaf=dp_table[leaf.t-1][leaf.k][leaf.b][leaf.right_id];
        right_result=backtrack_shape(right_leaf,path,dp_table);
        if(DEBUG) {
            assert(left_result.front().first == path[leaf.a]);
            assert(left_result.front().second[0].second == leaf.t - 1);
            assert(left_result.front().second.size() == 1);
            assert(left_result.back().first == k_id);
            assert(right_result.front().first == k_id);
            assert(right_result.back().first == path[leaf.b]);
            assert(right_result.back().second[0].second == leaf.t - 1);
            assert(left_result.back().second.size() == 1);
        }

        for(int i = 0; i < (int)left_result.size(); i++) {
            result.push_back(left_result[i]);
        }
        result.back().second.push_back(right_result.front().second.front());
        for(int i = 1; i < (int)right_result.size(); i++) {
            result.push_back(right_result[i]);
        }

        result.front().second[0].second++;
        result.back().second[0].second++;
        return result;
    }
    return Shape_vector{};
    // Handle unexpected Op value
    cerr << "[WernerAlgo::backtrack_shape] Warning: Unknown Op value encountered." << std::endl;
}
int WernerAlgo::split_dis(int s, int d, const WernerAlgo::ZLabel& L){
    if(L.op!=WernerAlgo::Op::MERGE||L.k<0) return 1000000000;
    int mid=(s+d)/2;
    return abs(mid-L.k);
}
pair<double,WernerAlgo::ZLabel> WernerAlgo::eval_best_J(
    int s, int d, int t, double alp, const DPTable& dp_table,
    const vector<double>& terminal_factors){
    double bestJ=1e18;
    int bestdis=1000000000;
    int flag=0;
    ZLabel tmp={};
    const auto& labels = dp_table[t][s][d];
    assert(labels.size() == terminal_factors.size());
    for(size_t label_index = 0;
        label_index < labels.size();
        ++label_index){
        const auto& L = labels[label_index];
        double J=(alp+L.B)*terminal_factors[label_index];
        int dis=split_dis(s,d,L);
        if(J+EPS<bestJ||(fabs(J-bestJ)<=EPS&&dis<bestdis)){
            bestJ=J;
            tmp=L;
            bestdis=dis;
            flag=1;
        }
    }
    if(flag) return {bestJ,tmp};
    else return {INF,tmp};
}

void WernerAlgo::run() {
    int round = 1;
    while (round-- && !requests.empty()) {
        variable_initialize();
        //cerr << "\033[1;31m"<< "[WernerAlgo's parameter] : "<< dpp.Zmin<<" "<<dpp.eps_bucket<<" "<<dpp.eta<< "\033[0m"<< endl;
        while (obj < 1.0) {
            Shape_vector shape=separation_oracle();
            if (shape.empty()) break;
            // 先用MyAlgo1的框架刻出來
            double q = 1.0;
            for(int i=0;i<shape.size();i++){
                map<int,int> need_amount;
                for(pair<int,int> usedtime:shape[i].second){
                    int start=usedtime.first,end=usedtime.second;
                    for(int t=start;t<=end;t++)
                        need_amount[t]++;
                }
                for(pair<int,int>P:need_amount){
                    int t=P.first;
                    double theta=P.second;
                    q=min(q,graph.get_node_memory_at(shape[i].first,t)/theta);
                }
            }
            if(q<=1e-10) break;
            int req_idx=-1;
            for(int i=0;i<requests.size();i++){
                int ln=shape.front().first,rn=shape.back().first;
                if(requests[i]==make_pair(ln,rn)){
                    if(req_idx==-1||alpha[req_idx]>alpha[i]){
                        req_idx=i;
                    }
                }
            }
            if(req_idx==-1) break;
            dirty_alpha_idxs[req_idx] = 1;
            x[req_idx][shape]+=q;
            double ori=alpha[req_idx];
            alpha[req_idx]=alpha[req_idx]*(1+epsilon*q);
            obj+=(alpha[req_idx]-ori);
            for(int i=0;i<shape.size();i++){
                map<int,int> need_amount;
                for(pair<int,int> usedtime:shape[i].second){
                    int start=usedtime.first,end=usedtime.second;
                    for(int t=start;t<=end;t++)
                        need_amount[t]++;
                }

                for(pair<int, int> P : need_amount) {
                    int t = P.first;
                    int node_id = shape[i].first;
                    double theta = P.second;
                    double original = beta[node_id][t];
                    if(graph.get_node_memory_at(node_id, t) == 0) {
                        beta[node_id][t] = INF;
                    } else {
                        beta[node_id][t] = beta[node_id][t] * (1 + epsilon * (q / (graph.get_node_memory_at(node_id, t) / theta)));
                    }
                    obj += (beta[node_id][t] - original) * graph.get_node_memory_at(node_id, t);
                }
                dirty_nodes[shape[i].first] = 1;
            }
            /* cerr<<"[WernerAlgo] obj :"<<obj<<endl;
            for(int i=0;i<shape.size();i++){
                cerr<<shape[i].first<<" : ";
                for(int j=0;j<shape[i].second.size();j++)
                    cerr<<"{"<<shape[i].second[j].first<<","<<shape[i].second[j].second<<"}  ";
                    cerr<<"\n";
            }
            cerr<<"=========\n"; */
        }
        vector<pair<double, Shape_vector>> shapes;

        for(int i = 0; i < (int)requests.size(); i++) {
            for(auto P : x[i]) {
                shapes.push_back({P.second, P.first});
                // shapes.push_back({Shape(P.first).get_fidelity(A, B, n, T, tao), P.first});
            }
        }

        // sort(shapes.begin(), shapes.end(), [](pair<double, Shape_vector> left, pair<double, Shape_vector> right) {
        //     if(fabs(left.first - right.first) >= EPS) return left.first > right.first;
        //     if(left.second.size() != right.second.size()) return left.second.size() < right.second.size();
        //     return left.second < right.second;
        // });
        /* sort(shapes.begin(), shapes.end(),
        [this](const pair<double, Shape_vector>& L,const pair<double, Shape_vector>& R){
        Shape sL(L.second), sR(R.second);
        double fL = sL.get_fidelity(A, B, n, T, tao, this->graph.get_F_init());
        double fR = sR.get_fidelity(A, B, n, T, tao, this->graph.get_F_init());
        if (fL < 0.0) fL = 0.0;
        if (fR < 0.0) fR = 0.0;
         // path success probability
        double pL = max(this->graph.path_Pr(sL), 1e-12);
        double pR = max(this->graph.path_Pr(sR), 1e-12);
         // score = x_weight * fidelity * path_Pr
        double scoreL = L.first  * pL;
        double scoreR = R.first  * pR;
        if (fabs(scoreL - scoreR) > EPS) return scoreL > scoreR;
        return scoreL>scoreR;
     }); */
        sort(shapes.begin(), shapes.end(), [](pair<double, Shape_vector> left, pair<double, Shape_vector> right) {
            return left.first > right.first;
        });
        // cerr << "[MyAlgo1] " << shapes.size() << endl;
        vector<bool> used(requests.size(), false);
        vector<int> finished;
        for(pair<double, Shape_vector> P : shapes) {
            Shape shape = Shape(P.second);
            int request_index = -1;
            for(int i = 0; i < (int)requests.size(); i++) {
                if(used[i] == false && requests[i] == make_pair(shape.get_node_mem_range().front().first, shape.get_node_mem_range().back().first)) {
                    request_index = i;
                }
            }

            if(request_index == -1 || used[request_index]) continue;
            if(graph.check_resource(shape)) {
                used[request_index] = true;
                // cerr << "[MyAlgo1] " << P.first << " " << P.second.size() << endl;
                graph.reserve_shape(shape);
                finished.push_back(request_index);
            }
        }

        sort(finished.rbegin(), finished.rend());
        for(auto fin : finished) {
            requests.erase(requests.begin() + fin);
        }
    }
    update_res();
    cerr << "[" << algorithm_name << "] end" << endl;
}
 
