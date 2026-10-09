#include "MyAlgo1.h"

MyAlgo1::MyAlgo1(const Graph& graph, const vector<pair<int, int>>& requests, const map<SDpair, vector<Path>>& paths):
    AlgorithmBase(graph, requests, paths) {
    algorithm_name = "MyAlgo1";

}

void MyAlgo1::variable_initialize() {
    // m = i + vt
    // x(i, m) = 0
    // delta = (1 + eps)((1 + eps)m)^(-1/eps)
    // alpha(i) = delta 
    // beta(v, t) = delta / C(v)

    double m = requests.size() + (double)graph.get_num_nodes() * (double)graph.get_time_limit();
    double delta = (1 + epsilon) * (1.0 / pow((1 + epsilon) * m, 1.0 / epsilon));
    obj = m * delta;
    // cerr << "[MyAlgo1] delta = " << delta << endl;
    x.clear();
    alpha.clear();
    beta.clear();
    x.resize(requests.size());
    alpha.resize(requests.size(), delta);
    beta.resize(graph.get_num_nodes(), vector<double>(graph.get_time_limit()));
    groups.clear();
    request_group.assign(requests.size(), -1);
    request_cache.assign(requests.size(), RequestEvaluation{});
    dirty_nodes.assign(graph.get_num_nodes(), 0);
    dirty_alpha.assign(requests.size(), 0);
    map<SDpair, int> group_index;
    for(int i = 0; i < (int)requests.size(); i++) {
        auto inserted = group_index.emplace(requests[i], (int)groups.size());
        if(inserted.second) {
            RequestGroup group;
            set<int> nodes;
            for(const Path& path : get_paths(requests[i].first, requests[i].second)) {
                nodes.insert(path.begin(), path.end());
                PathEvaluation evaluation;
                evaluation.probability = graph.path_Pr(path);
                group.paths.push_back(std::move(evaluation));
            }
            group.nodes.assign(nodes.begin(), nodes.end());
            groups.push_back(std::move(group));
        }
        request_group[i] = inserted.first->second;
        groups[request_group[i]].request_indices.push_back(i);
    }

    for(int i = 0; i < graph.get_num_nodes(); i++) {
        for(int t = 0; t < graph.get_time_limit(); t++) {
            if(graph.get_node_memory_at(i, t) == 0) beta[i][t] = INF;
            else beta[i][t] = delta / graph.get_node_memory_at(i, t);
        }
    }
}
Shape_vector MyAlgo1::separation_oracle() {
    // A group becomes stale when a node on its paths or the alpha of one of
    // its requests changed; staleness persists until it is recomputed.
    for(RequestGroup& group : groups) {
        if(group.stale) continue;
        for(int node : group.nodes) {
            if(dirty_nodes[node]) { group.stale = true; break; }
        }
        for(int i : group.request_indices) {
            if(group.stale) break;
            if(dirty_alpha[i]) group.stale = true;
        }
    }
    fill(dirty_nodes.begin(), dirty_nodes.end(), 0);
    fill(dirty_alpha.begin(), dirty_alpha.end(), 0);

    // The DP is an exact minimum over sums of betas, and alpha and beta only
    // grow, so a stale group's previous value is a lower bound on its current
    // value. Stale groups are recomputed in bound order until none can beat
    // (or tie) the best current value, which retains the request-order tie
    // rule of the eager oracle.
    Shape_vector min_shape;
    while(true) {
        double min_value = INF;
        int min_request = -1;
        for(int i = 0; i < (int)requests.size(); i++) {
            if(groups[request_group[i]].stale) continue;
            if(request_cache[i].value < min_value) {
                min_value = request_cache[i].value;
                min_request = i;
            }
        }

        int next = -1;
        for(int g = 0; g < (int)groups.size(); g++) {
            const RequestGroup& group = groups[g];
            if(!group.stale) continue;
            if(group.lower_bound >= 0 &&
               (group.lower_bound >= INF || group.lower_bound > min_value))
                continue;
            if(next == -1 || group.lower_bound < groups[next].lower_bound)
                next = g;
        }
        if(next == -1) {
            if(min_request >= 0) min_shape = request_cache[min_request].shape;
            break;
        }

        RequestGroup& group = groups[next];
        recompute_group(group);
        double bound = INF;
        for(int i : group.request_indices) {
            request_cache[i] = evaluate_group(group, alpha[i]);
            bound = min(bound, request_cache[i].value);
        }
        group.lower_bound = bound;
        group.stale = false;
    }
    return min_shape;
}
void MyAlgo1::recompute_group(RequestGroup& group) {
    const SDpair& request = requests[group.request_indices.front()];
    const vector<Path>& paths = get_paths(request.first, request.second);
    for(int p = 0; p < (int)paths.size(); p++) {
        const Path& path = paths[p];
        dp.resize(path.size());
        par.resize(path.size());
        caled.resize(path.size());
        for(int i = 0; i < (int)path.size(); i++) {
            dp[i].resize(path.size());
            par[i].resize(path.size());
            caled[i].resize(path.size());
            for(int j = 0; j < (int)path.size(); j++) {
                dp[i][j].resize(time_limit);
                par[i][j].assign(time_limit, -2);
                caled[i][j].assign(time_limit, false);
            }
        }

        PathEvaluation& evaluation = group.paths[p];
        evaluation.cost.assign(time_limit, INF);
        for(int t = 0; t < time_limit; t++)
            evaluation.cost[t] = recursion_calculate_min_shape(0, path.size() - 1, t, path);
        // Keep the parents so only shapes that are actually selected are
        // backtracked; the scalar DP is cheaper than backtracking every slot.
        evaluation.par = par;
        evaluation.shape.assign(time_limit, Shape_vector{});
        evaluation.has_shape.assign(time_limit, 0);
    }
}
MyAlgo1::RequestEvaluation MyAlgo1::evaluate_group(RequestGroup& group, double alp) {
    RequestEvaluation best_evaluation;
    double best_cost = INF;
    for(int p = 0; p < (int)group.paths.size(); p++) {
        PathEvaluation& evaluation = group.paths[p];
        double best = INF;
        int best_time = -1;
        for(int t = 0; t < time_limit; t++) {
            double result = (evaluation.cost[t] + alp) / evaluation.probability;
            if(best > result) {
                best = result;
                best_time = t;
            }
        }

        if(best_time == -1) continue;

        if(best < best_cost) {
            if(!evaluation.has_shape[best_time]) {
                const SDpair& request = requests[group.request_indices.front()];
                const Path& path = get_paths(request.first, request.second)[p];
                par.swap(evaluation.par);
                evaluation.shape[best_time] =
                    recursion_find_shape(0, (int)path.size() - 1, best_time, path);
                par.swap(evaluation.par);
                evaluation.has_shape[best_time] = 1;
            }
            best_evaluation.shape = evaluation.shape[best_time];
            best_cost = best;
        }
    }

    if(best_cost == INF) return {{}, INF};
    best_evaluation.value = best_cost;
    return best_evaluation;
}
double MyAlgo1::recursion_calculate_min_shape(int left, int right, int t, const vector<int> &path) {
    if(t <= 0) return INF;
    // cerr << left << " " << right << " " << t << " " << (int)path.size() << endl;

    int left_id = path[left], right_id = path[right];
    if(left == right - 1) {
        return beta[left_id][t - 1] + beta[left_id][t] + beta[right_id][t - 1] + beta[right_id][t];
    }

    if(caled[left][right][t]) return dp[left][right][t];
    
    double best = recursion_calculate_min_shape(left, right, t - 1, path);
    int best_k = -1;
    int best_dis_mid = (right - left + 1);
    for(int k = left + 1; k < right; k++) {
        double left_result = recursion_calculate_min_shape(left, k, t - 1, path);
        double right_result = recursion_calculate_min_shape(k, right, t - 1, path);
        double result = left_result + right_result;
        int dis_mid = abs((left + right) / 2 - k);
        if(result + EPS < best || (fabs(result - best) <= EPS && dis_mid < best_dis_mid)) {
            best = result;
            best_k = k;
            best_dis_mid = dis_mid;
        }
    }

    caled[left][right][t] = true;
    par[left][right][t] = best_k;
    return dp[left][right][t] = best + beta[left_id][t] + beta[right_id][t];
}
Shape_vector MyAlgo1::recursion_find_shape(int left, int right, int t, const vector<int> &path) {
    int left_id = path[left], right_id = path[right], k = par[left][right][t];
    if(left == right - 1 && k == -2) {
        Shape_vector result;
        result.push_back({left_id, {{t - 1, t}}});
        result.push_back({right_id, {{t - 1, t}}});
        return result;    
    }

    if(k == -1) {
        Shape_vector last_time = recursion_find_shape(left, right, t - 1, path);
        if(DEBUG) {
            assert(last_time.front().first == left_id);
            assert(last_time.front().second[0].second == t - 1);
            assert(last_time.back().first == right_id);
            assert(last_time.back().second[0].second == t - 1);
        }
        last_time.front().second[0].second++;
        last_time.back().second[0].second++;
        return last_time;
    }

    assert(k >= 0);
    Shape_vector left_result, right_result, result;

    int k_id = path[k];
    left_result = recursion_find_shape(left, k, t - 1, path);
    right_result = recursion_find_shape(k, right, t - 1, path);

    if(DEBUG) {
        assert(left_result.front().first == left_id);
        assert(left_result.front().second[0].second == t - 1);
        assert(left_result.front().second.size() == 1);
        assert(left_result.back().first == k_id);
        assert(right_result.front().first == k_id);
        assert(right_result.back().first == right_id);
        assert(right_result.back().second[0].second == t - 1);
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

void MyAlgo1::run() {

    // q = min (1, C(v) / theta(v, t)) for all v, t

    // primal:
    // x(i, m) = x(i, m) + q

    // dual:
    // alpha(v) = alpha(v)(1 + eps(q / ahpla(v))
    // beta(v, t) = beta(v, t)(1 + eps(q / beta(v, t))

    int round = 1;
    while(round-- && !requests.empty()) {
        variable_initialize();
        while(obj < 1.0) {
            // cerr << "obj = " << obj << endl;
            Shape_vector shape = separation_oracle();
            if(shape.empty()) break;
            double q = 1;
            for(int i = 0; i < (int)shape.size(); i++) {
                map<int, int> need_amount; // time to amount
                for(pair<int, int> rng : shape[i].second) {
                    int left = rng.first, right = rng.second;
                    for(int t = left; t <= right; t++) {
                        need_amount[t]++;
                    }
                }

                for(pair<int, int> P : need_amount) {
                    int t = P.first;
                    double theta = P.second;
                    q = min(q, graph.get_node_memory_at(i, t) / theta);
                }
            }
            if(q <= 1e-10) break;

            // Node-time demand of this shape; reused by every dual update.
            map<pair<int, int>, int> total_need;
            for(int i = 0; i < (int)shape.size(); i++) {
                for(pair<int, int> rng : shape[i].second) {
                    for(int t = rng.first; t <= rng.second; t++)
                        total_need[{shape[i].first, t}]++;
                }
            }

            // Reuse this schedule while its dual cost stays close to the
            // initial cost, exactly as WPFA does.
            double initial_cost = 0;
            for(int reuse = 0; reuse < oracle_reuse && obj < 1.0; ++reuse) {
                int request_index = -1;
                for(int i = 0; i < (int)requests.size(); i++) {
                    if(requests[i] == make_pair(shape.front().first, shape.back().first)) {
                        if(request_index == -1 || alpha[request_index] > alpha[i]) {
                            request_index = i;
                        }
                    }
                }
                if(request_index == -1) break;
                if(oracle_reuse > 1) {
                    double current_cost = alpha[request_index];
                    for(const auto& slot : total_need)
                        current_cost += slot.second * beta[slot.first.first][slot.first.second];
                    if(reuse == 0) initial_cost = current_cost;
                    // Heuristic guard; this does not bound final rounded quality.
                    else if(current_cost > initial_cost * (1 + reuse_cost_growth)) break;
                }

                x[request_index][shape] += q;
                dirty_alpha[request_index] = 1;

                double ori = alpha[request_index];
                alpha[request_index] = alpha[request_index] * (1 + epsilon * q);
                obj += (alpha[request_index] - ori);

                for(const auto& slot : total_need) {
                    int node_id = slot.first.first, t = slot.first.second;
                    dirty_nodes[node_id] = 1;
                    double theta = slot.second;
                    double original = beta[node_id][t];
                    if(graph.get_node_memory_at(node_id, t) == 0) {
                        beta[node_id][t] = INF;
                    } else {
                        beta[node_id][t] = beta[node_id][t] * (1 + epsilon * (q / (graph.get_node_memory_at(node_id, t) / theta)));
                    }
                    obj += (beta[node_id][t] - original) * graph.get_node_memory_at(node_id, t);
                }
            }

            // cerr << "[MyAlgo1] obj = " << obj << endl;
        }


        vector<pair<double, Shape_vector>> shapes;

        for(int i = 0; i < (int)requests.size(); i++) {
            for(const auto& P : x[i]) {
                shapes.push_back({P.second, P.first});
                // shapes.push_back({Shape(P.first).get_fidelity(A, B, n, T, tao), P.first});
            }
        }

        // sort(shapes.begin(), shapes.end(), [](const pair<double, Shape_vector>& left, const pair<double, Shape_vector>& right) {
        //     if(fabs(left.first - right.first) >= EPS) return left.first > right.first;
        //     if(left.second.size() != right.second.size()) return left.second.size() < right.second.size();
        //     return left.second < right.second;
        // });
        sort(shapes.begin(), shapes.end(), [](const pair<double, Shape_vector>& left, const pair<double, Shape_vector>& right) {
            return left.first > right.first;
        });
        // cerr << "[MyAlgo1] " << shapes.size() << endl;
        vector<bool> used(requests.size(), false);
        vector<int> finished;
        for(const auto& P : shapes) {
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
