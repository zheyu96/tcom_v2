#include "REPS.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

#ifdef REPS_USE_GUROBI
#include "gurobi_c++.h"
#define REPS_HAS_GUROBI 1
#else
#define REPS_HAS_GUROBI 0
#endif

using namespace std;

namespace {

constexpr double FLOW_TOLERANCE = 1e-9;

pair<int, int> undirected_edge(int u, int v) {
    return minmax(u, v);
}

} // namespace

REPS::REPS() {
    method_name = "REPS";
}

REPS::~REPS() {}

bool REPS::gurobi_available() {
    return REPS_HAS_GUROBI != 0;
}

void REPS::solve_fractional_flow(vector<double>& throughput,
                                 vector<FlowMap>& flows) {
    throughput.clear();
    flows.clear();

#if REPS_HAS_GUROBI
    try {
        GRBEnv environment(true);
        environment.set(GRB_IntParam_OutputFlag, DEBUG ? 1 : 0);
        environment.start();

        GRBModel model(environment);
        model.set(GRB_StringAttr_ModelName, "REPS_fractional_flow");

        const int request_count = (int)requests.size();
        const int node_count = graph.get_num_nodes();

        vector<map<DirectedEdge, GRBVar>> flow_variables(request_count);
        for(int request = 0; request < request_count; ++request) {
            for(int u = 0; u < node_count; ++u) {
                for(int v : graph.adj_list[u]) {
                    const string name = "f_" + to_string(request) + "_" +
                                        to_string(u) + "_" + to_string(v);
                    flow_variables[request][{u, v}] = model.addVar(
                        0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS, name);
                }
            }
        }

        vector<GRBVar> throughput_variables;
        throughput_variables.reserve(request_count);
        for(int request = 0; request < request_count; ++request) {
            throughput_variables.push_back(model.addVar(
                0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS,
                "t_" + to_string(request)));
        }

        vector<DirectedEdge> edges;
        map<DirectedEdge, GRBVar> allocated_pairs;
        for(int u = 0; u < node_count; ++u) {
            for(int v : graph.adj_list[u]) {
                if(u >= v) continue;
                const DirectedEdge edge = {u, v};
                edges.push_back(edge);
                allocated_pairs[edge] = model.addVar(
                    0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS,
                    "x_" + to_string(u) + "_" + to_string(v));
            }
        }

        GRBLinExpr objective = 0.0;
        for(const GRBVar& variable : throughput_variables) {
            objective += variable;
        }
        model.setObjective(objective, GRB_MAXIMIZE);

        // Per-request flow conservation.  A positive value is flow leaving
        // the source; the destination therefore has the corresponding
        // negative balance.
        for(int request = 0; request < request_count; ++request) {
            const int src = requests[request].first;
            const int dst = requests[request].second;
            for(int u = 0; u < node_count; ++u) {
                GRBLinExpr balance = 0.0;
                for(int v : graph.adj_list[u]) {
                    balance += flow_variables[request].at({u, v});
                    balance -= flow_variables[request].at({v, u});
                }

                if(u == src) {
                    model.addConstr(
                        balance == throughput_variables[request],
                        "flow_source_" + to_string(request));
                } else if(u == dst) {
                    model.addConstr(
                        balance == GRBLinExpr(
                            throughput_variables[request], -1.0),
                        "flow_destination_" + to_string(request));
                } else {
                    model.addConstr(
                        balance == 0.0,
                        "flow_balance_" + to_string(request) + "_" +
                            to_string(u));
                }
            }
        }

        // An allocated elementary pair on an undirected link succeeds with
        // probability p.  Both flow directions share that link allocation.
        for(const DirectedEdge& edge : edges) {
            const int u = edge.first;
            const int v = edge.second;
            GRBLinExpr link_flow = 0.0;
            for(int request = 0; request < request_count; ++request) {
                link_flow += flow_variables[request].at({u, v});
                link_flow += flow_variables[request].at({v, u});
            }
            const double success_probability =
                graph.get_entangle_succ_prob(u, v);
            model.addConstr(
                link_flow <= allocated_pairs.at(edge) * success_probability,
                "link_capacity_" + to_string(u) + "_" + to_string(v));
        }

        // Each incident link allocation consumes one memory unit at the node.
        for(int u = 0; u < node_count; ++u) {
            GRBLinExpr memory_usage = 0.0;
            for(int v : graph.adj_list[u]) {
                memory_usage += allocated_pairs.at(undirected_edge(u, v));
            }
            model.addConstr(
                memory_usage <= graph.get_node_memory(u),
                "node_memory_" + to_string(u));
        }

        model.optimize();
        const int status = model.get(GRB_IntAttr_Status);
        if(status != GRB_OPTIMAL) {
            throw runtime_error(
                "REPS fractional-flow LP did not reach an optimal solution "
                "(Gurobi status " + to_string(status) + ")");
        }

        throughput.resize(request_count, 0.0);
        flows.resize(request_count);
        for(int request = 0; request < request_count; ++request) {
            throughput[request] =
                throughput_variables[request].get(GRB_DoubleAttr_X);
            for(int u = 0; u < node_count; ++u) {
                for(int v : graph.adj_list[u]) {
                    const double value = flow_variables[request]
                                             .at({u, v})
                                             .get(GRB_DoubleAttr_X);
                    if(value > FLOW_TOLERANCE) {
                        flows[request][{u, v}] = value;
                    }
                }
            }
        }
    } catch(const GRBException& error) {
        throw runtime_error(
            "REPS Gurobi error " + to_string(error.getErrorCode()) +
            ": " + error.getMessage());
    }
#else
    throw runtime_error(
        "REPS requires Gurobi. Recompile REPS.cpp with "
        "-DREPS_USE_GUROBI, add Gurobi's include path, and link "
        "gurobi_c++ plus the installed Gurobi version library.");
#endif
}

pair<Path, double> REPS::widest_path(int src,
                                     int dst,
                                     const FlowMap& flow) {
    if(src == dst) return {{}, 0.0};

    const int node_count = graph.get_num_nodes();
    vector<double> width(node_count, 0.0);
    vector<int> parent(node_count, -1);
    vector<bool> visited(node_count, false);
    priority_queue<pair<double, int>> queue;

    width[src] = numeric_limits<double>::infinity();
    queue.push({width[src], src});

    while(!queue.empty()) {
        const int u = queue.top().second;
        queue.pop();
        if(visited[u]) continue;
        visited[u] = true;
        if(u == dst) break;

        for(int v : graph.adj_list[u]) {
            const auto flow_it = flow.find({u, v});
            if(flow_it == flow.end() || flow_it->second <= FLOW_TOLERANCE) {
                continue;
            }
            const double candidate = min(width[u], flow_it->second);
            if(candidate > width[v] + FLOW_TOLERANCE) {
                width[v] = candidate;
                parent[v] = u;
                queue.push({candidate, v});
            }
        }
    }

    if(!visited[dst] || width[dst] <= FLOW_TOLERANCE) {
        return {{}, 0.0};
    }

    Path path;
    for(int node = dst; node != -1; node = parent[node]) {
        path.push_back(node);
        if(node == src) break;
    }
    if(path.empty() || path.back() != src) return {{}, 0.0};
    reverse(path.begin(), path.end());
    return {path, width[dst]};
}

void REPS::subtract_path_flow(const Path& path,
                              double amount,
                              FlowMap& flow) {
    for(size_t index = 1; index < path.size(); ++index) {
        const DirectedEdge edge = {path[index - 1], path[index]};
        auto flow_it = flow.find(edge);
        if(flow_it == flow.end() ||
           flow_it->second + FLOW_TOLERANCE < amount) {
            throw runtime_error(
                "REPS flow decomposition encountered an invalid residual");
        }
        flow_it->second = max(0.0, flow_it->second - amount);
    }
}

void REPS::build_paths(Graph _graph, vector<SDpair> _requests) {
    paths.clear();
    graph = _graph;
    requests = std::move(_requests);
    if(requests.empty()) return;

    vector<double> throughput;
    vector<FlowMap> flows;
    solve_fractional_flow(throughput, flows);

    map<SDpair, set<Path>> unique_paths;
    for(size_t request = 0; request < requests.size(); ++request) {
        while(true) {
            Path path;
            double width = 0.0;
            tie(path, width) = widest_path(
                requests[request].first,
                requests[request].second,
                flows[request]);
            if(path.empty() || width <= FLOW_TOLERANCE) break;
            unique_paths[requests[request]].insert(path);
            subtract_path_flow(path, width, flows[request]);
        }

        if(throughput[request] > FLOW_TOLERANCE &&
           unique_paths[requests[request]].empty()) {
            throw runtime_error(
                "REPS LP returned positive throughput but no path could be "
                "decomposed for request " + to_string(request));
        }
    }

    size_t path_count = 0;
    for(const auto& entry : unique_paths) {
        paths[entry.first] = vector<Path>(entry.second.begin(),
                                          entry.second.end());
        path_count += entry.second.size();
    }
    cerr << "[REPS] generated " << path_count << " distinct paths for "
         << paths.size() << " source-destination pairs." << endl;
}
