#!/usr/bin/env bash
set -euo pipefail

workspace=/mnt/c/Users/USER/Desktop/lab/ICC26/werner_cur_version_TCOM_R1_TNET_setting
preview_dir=$(mktemp -d /tmp/tau-accuracy-preview-XXXXXX)
cp -a "$workspace/." "$preview_dir/"
cd "$preview_dir/src"

sed -i 's/change_parameter\["tao"\] = {0.001,0.002,0.003,0.004,0.005};/change_parameter["tao"] = {0.001};/' main.cpp
sed -i 's/new WernerAlgo2(graph,requests,paths);/new WernerAlgo2(graph,requests,paths,0.35,0.001);/' main.cpp

g++ -fopenmp -O3 -march=native --std=c++17 \
    main.cpp ExperimentWorkload.o \
    Network/Node/Node.o Network/Shape/Shape.o Network/Graph/Graph.o \
    Algorithm/AlgorithmBase/AlgorithmBase.o config.o \
    Algorithm/MyAlgo1/MyAlgo1.o Algorithm/MyAlgo2/MyAlgo2.o \
    Algorithm/MyAlgo3/MyAlgo3.o Algorithm/MyAlgo4/MyAlgo4.o \
    Algorithm/MyAlgo5/MyAlgo5.o Algorithm/MyAlgo6/MyAlgo6.o \
    Algorithm/WernerAlgo/WernerAlgo.o \
    Algorithm/WernerAlgo2/WernerAlgo2.o \
    Algorithm/WernerAlgo3/WernerAlgo3.o \
    Algorithm/WernerAlgo_UB/WernerAlgo_UB.o \
    Algorithm/EFiRAP/EFiRAP.o \
    Algorithm/EFiRAP_longtime/EFiRAP_longtime.o \
    Network/PathMethod/PathMethodBase/PathMethod.o \
    Network/PathMethod/Greedy/Greedy.o \
    Network/PathMethod/QCAST/QCAST.o Network/PathMethod/REPS/REPS.o \
    -o tau_accuracy_preview.out

EXPERIMENT_X_NAME=tao ./tau_accuracy_preview.out \
    >preview.stdout 2>preview.stderr

printf 'PREVIEW_DIR=%s\n' "$preview_dir"
for result in ../data/ans/Greedy_tao_*.ans; do
    printf '### %s\n' "$(basename "$result")"
    cat "$result"
done
