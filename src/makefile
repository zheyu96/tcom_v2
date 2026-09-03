# --- 路徑與變數設定 ---
# 基礎依賴物件
dependency = Network/Node/Node.o Network/Shape/Shape.o Network/Graph/Graph.o Algorithm/AlgorithmBase/AlgorithmBase.o config.o
workload = ExperimentWorkload.o

# 完整演算法物件 (用於 a.out / main.cpp)
algorithm = Algorithm/MyAlgo1/MyAlgo1.o Algorithm/MyAlgo2/MyAlgo2.o Algorithm/MyAlgo3/MyAlgo3.o \
            Algorithm/MyAlgo4/MyAlgo4.o Algorithm/MyAlgo5/MyAlgo5.o Algorithm/MyAlgo6/MyAlgo6.o \
            Algorithm/WernerAlgo/WernerAlgo.o Algorithm/WernerAlgo2/WernerAlgo2.o \
            Algorithm/WernerAlgo3/WernerAlgo3.o \
            Algorithm/WernerAlgo_UB/WernerAlgo_UB.o Algorithm/EFiRAP/EFiRAP.o \
            Algorithm/EFiRAP_longtime/EFiRAP_longtime.o

# 時間量測專用演算法物件
algorithm_zfa = Algorithm/WernerAlgo_time/WernerAlgo_time.o 
algorithm_zfa2 = Algorithm/WernerAlgo2/WernerAlgo2.o
algorithm2 = Algorithm/MyAlgo3/MyAlgo3.o Algorithm/MyAlgo1_time/MyAlgo1_time.o
algorithm_runtime_compare = Algorithm/WernerAlgo2/WernerAlgo2.o \
                            Algorithm/EFiRAP/EFiRAP.o \
                            Algorithm/EFiRAP_longtime/EFiRAP_longtime.o
algorithm_main_time = Algorithm/WernerAlgo/WernerAlgo.o \
                      Algorithm/WernerAlgo2/WernerAlgo2.o \
                      Algorithm/WernerAlgo3/WernerAlgo3.o \
                      Algorithm/MyAlgo1/MyAlgo1.o \
                      Algorithm/MyAlgo3/MyAlgo3.o \
                      Algorithm/EFiRAP/EFiRAP.o \
                      Algorithm/EFiRAP_longtime/EFiRAP_longtime.o

# 路由與路徑方法
routing = Network/PathMethod/PathMethodBase/PathMethod.o Network/PathMethod/Greedy/Greedy.o \
          Network/PathMethod/QCAST/QCAST.o Network/PathMethod/REPS/REPS.o
routing_runtime_compare = Network/PathMethod/PathMethodBase/PathMethod.o \
                          Network/PathMethod/Greedy/Greedy.o

# 編譯旗標
gcc_flag = -O3 -fopenmp -march=native --std=c++17

# Gurobi configuration. GUROBI_HOME normally points to a directory such as
# /opt/gurobi1200/linux64. Both variables can also be supplied to make:
#   make gurobi_path=/home/oba/danger/gurobi951/linux64 gurobi_lib=gurobi95
gurobi_path ?= $(GUROBI_HOME)
gurobi_detected_so := $(firstword $(wildcard $(gurobi_path)/lib/libgurobi[0-9]*.so))
gurobi_detected_lib := $(patsubst lib%.so,%,$(notdir $(gurobi_detected_so)))
gurobi_lib ?= $(gurobi_detected_lib)

ifneq ($(strip $(gurobi_path)),)
ifneq ($(strip $(gurobi_lib)),)
gurobi_flag = -I$(gurobi_path)/include -L$(gurobi_path)/lib -lgurobi_c++ -l$(gurobi_lib)
else
$(error Gurobi found at $(gurobi_path), but its versioned libgurobi*.so was not detected; set gurobi_lib explicitly)
endif
else
gurobi_flag =
endif

# Passing a non-empty gurobi_flag enables both EFiRAP Gurobi-backed EPS models.
ifneq ($(strip $(gurobi_flag)),)
efirap_gurobi_define = -DEFIRAP_USE_GUROBI
else
efirap_gurobi_define =
endif

# --- 主要執行目標 ---

all: a.out

# 1. 執行 WernerAlgo_time 測試 (ZFA_runtime.cpp)
zfa_run: ZFA_runtime.cpp $(dependency) $(algorithm_zfa) $(routing)
	g++ -fopenmp -O3 $(gcc_flag) ZFA_runtime.cpp $(dependency) $(algorithm_zfa) $(routing) $(gurobi_flag) -o zfa_run
	./zfa_run 

# 2. 執行 WernerAlgo2 runtime 測試 (ZFA2_runtime.cpp)
zfa2_run: ZFA2_runtime.cpp $(dependency) $(algorithm_zfa2) $(routing)
	g++ -fopenmp -O3 $(gcc_flag) ZFA2_runtime.cpp $(dependency) $(algorithm_zfa2) $(routing) $(gurobi_flag) -o zfa2_run
	./zfa2_run

# 3. 舊有的時間量測目標
runtime: runtime.cpp $(dependency) $(algorithm2) $(routing)
	g++ -fopenmp $(gcc_flag) runtime.cpp $(dependency) $(algorithm2) $(routing) $(gurobi_flag) -o runtime

# Standalone, sequential WPFA/EFiRAP runtime benchmark. This target only
# builds the executable; run ./runtime_compare --help for benchmark options.
runtime_compare: runtime_compare.cpp $(workload) $(dependency) $(algorithm_runtime_compare) $(routing_runtime_compare)
	g++ -fopenmp $(gcc_flag) runtime_compare.cpp $(workload) $(dependency) $(algorithm_runtime_compare) $(routing_runtime_compare) $(gurobi_flag) -o runtime_compare

# Dedicated runtime-only experiment for request_cnt, fidelity_threshold, and
# time_limit. The executable writes raw/summary CSV and chart-compatible ANS.
main_time: main_time.cpp $(workload) $(dependency) $(algorithm_main_time) $(routing_runtime_compare)
	g++ -fopenmp $(gcc_flag) main_time.cpp $(workload) $(dependency) $(algorithm_main_time) $(routing_runtime_compare) $(gurobi_flag) -o main_time

# 4. 預設主程式目標 (包含 MyAlgo 1-6)
a.out: main.cpp $(workload) $(dependency) $(algorithm) $(routing)
	g++ -fopenmp $(gcc_flag) main.cpp $(workload) $(dependency) $(algorithm) $(routing) $(gurobi_flag) -o a.out
	> ../data/log/ZFA2_Purification_Stats.txt
	./a.out

# --- 各個物件檔案 (Object Files) 編譯規則 ---

# 網路核心組件
Network/Node/Node.o: Network/Node/Node.cpp Network/Node/Node.h config.h
	g++ -c $(gcc_flag) Network/Node/Node.cpp -o Network/Node/Node.o

Network/Shape/Shape.o: Network/Shape/Shape.cpp Network/Shape/Shape.h Network/Purification/Purification.h config.h
	g++ -c $(gcc_flag) Network/Shape/Shape.cpp -o Network/Shape/Shape.o

Network/Graph/Graph.o: Network/Graph/Graph.cpp Network/Graph/Graph.h Network/Purification/Purification.h config.h
	g++ -c $(gcc_flag) Network/Graph/Graph.cpp -o Network/Graph/Graph.o

Algorithm/AlgorithmBase/AlgorithmBase.o: Algorithm/AlgorithmBase/AlgorithmBase.h Algorithm/AlgorithmBase/AlgorithmBase.cpp config.h
	g++ -c $(gcc_flag) Algorithm/AlgorithmBase/AlgorithmBase.cpp -o Algorithm/AlgorithmBase/AlgorithmBase.o

# MyAlgo 1-6 編譯規則
Algorithm/MyAlgo1/MyAlgo1.o: Algorithm/MyAlgo1/MyAlgo1.cpp Algorithm/MyAlgo1/MyAlgo1.h config.h
	g++ -c $(gcc_flag) Algorithm/MyAlgo1/MyAlgo1.cpp -o Algorithm/MyAlgo1/MyAlgo1.o
Algorithm/MyAlgo2/MyAlgo2.o: Algorithm/MyAlgo2/MyAlgo2.cpp Algorithm/MyAlgo2/MyAlgo2.h config.h
	g++ -c $(gcc_flag) Algorithm/MyAlgo2/MyAlgo2.cpp -o Algorithm/MyAlgo2/MyAlgo2.o
Algorithm/MyAlgo3/MyAlgo3.o: Algorithm/MyAlgo3/MyAlgo3.cpp Algorithm/MyAlgo3/MyAlgo3.h config.h
	g++ -c $(gcc_flag) Algorithm/MyAlgo3/MyAlgo3.cpp -o Algorithm/MyAlgo3/MyAlgo3.o
Algorithm/MyAlgo4/MyAlgo4.o: Algorithm/MyAlgo4/MyAlgo4.cpp Algorithm/MyAlgo4/MyAlgo4.h config.h
	g++ -c $(gcc_flag) Algorithm/MyAlgo4/MyAlgo4.cpp -o Algorithm/MyAlgo4/MyAlgo4.o
Algorithm/MyAlgo5/MyAlgo5.o: Algorithm/MyAlgo5/MyAlgo5.cpp Algorithm/MyAlgo5/MyAlgo5.h config.h
	g++ -c $(gcc_flag) Algorithm/MyAlgo5/MyAlgo5.cpp -o Algorithm/MyAlgo5/MyAlgo5.o
Algorithm/MyAlgo6/MyAlgo6.o: Algorithm/MyAlgo6/MyAlgo6.cpp Algorithm/MyAlgo6/MyAlgo6.h config.h
	g++ -c $(gcc_flag) Algorithm/MyAlgo6/MyAlgo6.cpp -o Algorithm/MyAlgo6/MyAlgo6.o

# Werner 演算法編譯規則
Algorithm/WernerAlgo/WernerAlgo.o: Algorithm/WernerAlgo/WernerAlgo.cpp Algorithm/WernerAlgo/WernerAlgo.h config.h
	g++ -c $(gcc_flag) Algorithm/WernerAlgo/WernerAlgo.cpp -o Algorithm/WernerAlgo/WernerAlgo.o
Algorithm/WernerAlgo2/WernerAlgo2.o: Algorithm/WernerAlgo2/WernerAlgo2.cpp Algorithm/WernerAlgo2/WernerAlgo2.h Network/Purification/Purification.h config.h
	g++ -c $(gcc_flag) Algorithm/WernerAlgo2/WernerAlgo2.cpp -o Algorithm/WernerAlgo2/WernerAlgo2.o
Algorithm/WernerAlgo3/WernerAlgo3.o: Algorithm/WernerAlgo3/WernerAlgo3.cpp Algorithm/WernerAlgo3/WernerAlgo3.h config.h
	g++ -c $(gcc_flag) Algorithm/WernerAlgo3/WernerAlgo3.cpp -o Algorithm/WernerAlgo3/WernerAlgo3.o
Algorithm/WernerAlgo_UB/WernerAlgo_UB.o: Algorithm/WernerAlgo_UB/WernerAlgo_UB.cpp Algorithm/WernerAlgo_UB/WernerAlgo_UB.h config.h
	g++ -c $(gcc_flag) Algorithm/WernerAlgo_UB/WernerAlgo_UB.cpp -o Algorithm/WernerAlgo_UB/WernerAlgo_UB.o
Algorithm/EFiRAP/EFiRAP.o: Algorithm/EFiRAP/EFiRAP.cpp Algorithm/EFiRAP/EFiRAP.h Network/Purification/Purification.h config.h
	g++ -c $(gcc_flag) $(efirap_gurobi_define) $(gurobi_flag) Algorithm/EFiRAP/EFiRAP.cpp -o Algorithm/EFiRAP/EFiRAP.o
Algorithm/EFiRAP_longtime/EFiRAP_longtime.o: Algorithm/EFiRAP_longtime/EFiRAP_longtime.cpp Algorithm/EFiRAP_longtime/EFiRAP_longtime.h Network/Purification/Purification.h config.h
	g++ -c $(gcc_flag) $(efirap_gurobi_define) $(gurobi_flag) Algorithm/EFiRAP_longtime/EFiRAP_longtime.cpp -o Algorithm/EFiRAP_longtime/EFiRAP_longtime.o

# 時間量測版本演算法編譯規則
Algorithm/WernerAlgo_time/WernerAlgo_time.o: Algorithm/WernerAlgo_time/WernerAlgo_time.cpp Algorithm/WernerAlgo_time/WernerAlgo_time.h config.h
	g++ -c $(gcc_flag) Algorithm/WernerAlgo_time/WernerAlgo_time.cpp -o Algorithm/WernerAlgo_time/WernerAlgo_time.o
Algorithm/WernerAlgo2_time/WernerAlgo2_time.o: Algorithm/WernerAlgo2_time/WernerAlgo2_time.cpp Algorithm/WernerAlgo2_time/WernerAlgo2_time.h config.h
	g++ -c $(gcc_flag) Algorithm/WernerAlgo2_time/WernerAlgo2_time.cpp -o Algorithm/WernerAlgo2_time/WernerAlgo2_time.o
Algorithm/MyAlgo1_time/MyAlgo1_time.o: Algorithm/MyAlgo1_time/MyAlgo1_time.cpp Algorithm/MyAlgo1_time/MyAlgo1_time.h config.h
	g++ -c $(gcc_flag) Algorithm/MyAlgo1_time/MyAlgo1_time.cpp -o Algorithm/MyAlgo1_time/MyAlgo1_time.o

# 路由與路徑方法編譯規則 (REPS 需連結 Gurobi)
Network/PathMethod/REPS/REPS.o: Network/PathMethod/REPS/REPS.cpp Network/PathMethod/REPS/REPS.h config.h
	g++ -c $(gcc_flag) Network/PathMethod/REPS/REPS.cpp $(gurobi_flag) -o Network/PathMethod/REPS/REPS.o
Network/PathMethod/Greedy/Greedy.o: Network/PathMethod/Greedy/Greedy.cpp Network/PathMethod/Greedy/Greedy.h config.h
	g++ -c $(gcc_flag) Network/PathMethod/Greedy/Greedy.cpp -o Network/PathMethod/Greedy/Greedy.o
Network/PathMethod/QCAST/QCAST.o: Network/PathMethod/QCAST/QCAST.cpp Network/PathMethod/QCAST/QCAST.h config.h
	g++ -c $(gcc_flag) Network/PathMethod/QCAST/QCAST.cpp -o Network/PathMethod/QCAST/QCAST.o
Network/PathMethod/PathMethodBase/PathMethod.o: Network/PathMethod/PathMethodBase/PathMethod.cpp Network/PathMethod/PathMethodBase/PathMethod.h config.h
	g++ -c $(gcc_flag) Network/PathMethod/PathMethodBase/PathMethod.cpp -o Network/PathMethod/PathMethodBase/PathMethod.o

config.o: config.h config.cpp
	g++ -c $(gcc_flag) config.cpp -o config.o

ExperimentWorkload.o: ExperimentWorkload.cpp ExperimentWorkload.h Network/Graph/Graph.h config.h
	g++ -c $(gcc_flag) ExperimentWorkload.cpp -o ExperimentWorkload.o

# --- 清除工具 ---
clean:
	rm -f a.out zfa_run zfa2_run runtime runtime_compare runtime_compare.exe main_time main_time.exe
	find . -type f -name '*.o' -delete
	find . -type f -name '*.h.gch' -delete
	rm -f ../data/ans/*.ans
	rm -f ../data/log/*
	rm -f ../data/input/*

.PHONY: clean all zfa_run zfa2_run runtime runtime_compare main_time
