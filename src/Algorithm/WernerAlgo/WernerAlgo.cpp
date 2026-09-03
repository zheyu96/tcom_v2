#include "WernerAlgo.h"

WernerAlgo::WernerAlgo(const Graph& graph,const vector<pair<int,int>>& requests,const map<SDpair, vector<Path>>& paths): AlgorithmBase(graph, requests, paths)
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
    dpp.eps_bucket = graph.get_bucket_eps();
    double F_th=graph.get_fidelity_threshold();
    double w_th=(4.0*F_th-1.0)/3.0;
    dpp.Zhat = sqrt(-log(w_th))+1e-9;
    dpp.Zmin = graph.get_Zmin();
    dpp.T    = time_limit-1;
    dpp.eta  = graph.get_tao()/graph.get_T();
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
    for (int i = 0; i < (int)requests.size(); i++) {
        oracle_cache[i].resize(get_paths(requests[i].first, requests[i].second).size());
    }
    request_groups.clear();
    map<SDpair, size_t> request_group_index;
    for(int i = 0; i < (int)requests.size(); i++) {
        auto inserted = request_group_index.emplace(
            requests[i], request_groups.size());
        if(inserted.second) request_groups.emplace_back();
        request_groups[inserted.first->second].push_back(i);
    }
    oracle_worker_count = max(1, omp_get_max_threads());
    if(oracle_worker_count >= 8 &&
       oracle_worker_count == omp_get_num_procs()) {
        oracle_worker_count--;
    }
    dp_workspaces.clear();
    dp_workspaces.resize(oracle_worker_count);
    dirty_nodes.clear();
    dirty_alpha_idxs.clear();
}

Shape_vector WernerAlgo::separation_oracle(){
    double most_violate=1e9;
    Shape_vector todo_shape;

    vector<vector<unsigned char>> available(requests.size());
    for(int i = 0; i < (int)requests.size(); i++)
        available[i].assign(oracle_cache[i].size(), 0);

    struct PathTask {
        const Path* path = nullptr;
        int path_index = -1;
        vector<int> request_indices;
        vector<double> edge_Z;
        double path_pr = 0.0;
    };
    vector<PathTask> tasks;

    // Build one task per dirty (unique SD pair, path), not per request.
    for(const vector<int>& group : request_groups) {
        if(group.empty()) continue;
        const int representative = group.front();
        const int src = requests[representative].first;
        const int dst = requests[representative].second;
        const vector<Path>& cur_paths = get_paths(src, dst);

        for(int p = 0; p < (int)cur_paths.size(); p++) {
            bool path_dirty = false;
            for(int v : cur_paths[p]) {
                if(dirty_nodes.count(v)) {
                    path_dirty = true;
                    break;
                }
            }

            vector<int> recompute;
            recompute.reserve(group.size());
            for(int i : group) {
                const auto& cache = oracle_cache[i][p];
                if(cache.valid && !dirty_alpha_idxs.count(i) && !path_dirty)
                    available[i][p] = 1;
                else
                    recompute.push_back(i);
            }
            if(recompute.empty()) continue;

            PathTask task;
            task.path = &cur_paths[p];
            task.path_index = p;
            task.request_indices = std::move(recompute);
            task.edge_Z.resize(cur_paths[p].size() - 1);
            for(int edge = 0; edge + 1 < (int)cur_paths[p].size(); edge++) {
                task.edge_Z[edge] = sqrt(graph.get_edge_W(
                    cur_paths[p][edge], cur_paths[p][edge + 1])) + dpp.eta;
            }
            task.path_pr = graph.path_Pr(cur_paths[p]);
            tasks.push_back(std::move(task));
        }
    }

    #pragma omp parallel for schedule(dynamic, 1) if(tasks.size() > 1) \
        num_threads(oracle_worker_count)
    for(int task_index = 0; task_index < (int)tasks.size(); task_index++) {
        const PathTask& task = tasks[task_index];
        const Path& path = *task.path;
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
            run_dp_in_t(path, dpp, t, task.edge_Z, dp_table);

        for(int i : task.request_indices) {
            double local_best_J = 1e18;
            ZLabel local_best_label;
            for(int t = 1; t <= dpp.T; t++) {
                auto cur_val = eval_best_J(
                    0, path.size() - 1, t, alpha[i], dp_table);
                if(cur_val.first < local_best_J) {
                    local_best_J = cur_val.first;
                    local_best_label = cur_val.second;
                }
            }

            if(local_best_J < 1e18) {
                auto& cache = oracle_cache[i][task.path_index];
                cache.shape = backtrack_shape(
                    local_best_label, path, dp_table);
                cache.path_pr = task.path_pr;
                cache.best_score = local_best_J / cache.path_pr;
                cache.valid = true;
                available[i][task.path_index] = 1;
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

    dirty_nodes.clear();
    dirty_alpha_idxs.clear();
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

    // -------- t = 1..T-1 外圈時間迴圈 --------
    for(int a=0;a<n-1;a++)
        for(int b=a+1;b<n;b++){
            int s=path[a],e=path[b];
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
                    double Bp=pre[p_id].B+beta[s][t]+beta[e][t];
                    ZLabel L(Bp,Zp,Op::CONT,a,b,t,-1,p_id);
                    cand.push_back(std::move(L));
                }
            }
            //merge
            for(int k=a+1;k<b;k++){
                const auto& L1=dp_table[t-1][a][k];
                const auto& L2=dp_table[t-1][k][b];
                if(L1.size()==0||L2.size()==0) continue;
                for(int lid=0;lid<L1.size();lid++)
                    for(int rid=0;rid<L2.size();rid++){
                        const auto& left_seg=L1[lid];
                        const auto& right_seg=L2[rid];
                        const double left_Z = left_seg.Z + dpp.eta;
                        const double right_Z = right_seg.Z + dpp.eta;
                        const double Z_squared =
                            left_Z * left_Z + right_Z * right_Z;
                        if(Z_squared > reject_Z_squared) continue;
                        double Zp=sqrt(Z_squared);
                        if(Zp<=dpp.Zhat){
                            double Bp=left_seg.B+right_seg.B+beta[s][t]+beta[e][t];
                            ZLabel L(Bp,Zp,Op::MERGE,a,b,t,k,-1,lid,rid);
                            cand.push_back(std::move(L));
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
    double q=1+dpp.eps_bucket;
    double invLogQ=1.0/log(q);
    map<double,size_t> buckets;
    for(size_t index = 0; index < cand.size(); index++){
        const ZLabel& L = cand[index];
        double k;
        if(L.Z<=dpp.Zmin) k=0.0;
        else{
            k=floor(log(L.Z/dpp.Zmin)*invLogQ+1e-12);
            if(k<0) k=0.0;
        }
        auto existing = buckets.find(k);
        if(existing == buckets.end()) {
            buckets.emplace(k, index);
        } else if(L.B + 1e-12 < cand[existing->second].B) {
            existing->second = index;
        }
    }
    vector<ZLabel> bucketed;
    bucketed.reserve(buckets.size());
    for(const auto& entry : buckets)
        bucketed.push_back(std::move(cand[entry.second]));
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
    int s, int d, int t, double alp, const DPTable& dp_table){
    double bestJ=1e18;
    int bestdis=1000000000;
    int flag=0;
    ZLabel tmp={};
    for(const auto& L:dp_table[t][s][d]){
        double J=(alp+L.B)*exp(L.Z*L.Z);
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
            dirty_alpha_idxs.insert(req_idx);
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
                dirty_nodes.insert(shape[i].first);
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
 
