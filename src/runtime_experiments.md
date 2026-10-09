# Runtime experiment sweeps

`main_runtime.cpp` shares its implementation with `main_time.cpp`. Both now
support ten parameter sweeps used by the current Greedy figures, excluding path_set.
SmallScale's exhaustive OPT comparison remains a separate experiment driver.

| Sweep | Default values | Custom option |
| --- | --- | --- |
| request_cnt | 80,100,120,140,160 | --request-counts |
| fidelity_threshold | 0.70,0.75,0.80,0.85,0.90 | --fidelity-thresholds |
| time_limit | 7,9,11,13,15,17 | --time-limits |
| avg_memory | 4,6,8,10,12,14,16,20 | --avg-memories |
| tao | 0.001,0.002,0.003,0.004,0.005 seconds | --slot-durations |
| swap_prob | 0.6,0.7,0.8,0.9,0.95 | --swap-probs |
| hop_count | 1,2,3,4,5,6 | --hop-counts |
| mem_vary | 1,2,3,4,5 | --memory-variations |
| topo_vary | 0=Waxman,1=Grid,2=RGG | --topologies |
| mem_distribution | 0=uniform,1=degree-proportional,2=inverse-degree,3=heterogeneous | --memory-distributions |

The three original runtime sweep ranges are retained. Newly added ranges match
the corresponding `main.cpp` experiments. Other fixed settings are 100 nodes,
100 requests, 13 slots, average memory 10, slot duration 0.002 s, swapping
probability 0.9 and fidelity threshold 0.8. Epsilon, bucket width, oracle reuse,
seeds, threads, warmups and repetitions retain their runtime-driver controls.

## Run

From `src`, with Gurobi configured for the existing makefile:

```sh
make main_runtime
./main_runtime --sweeps all
```

Omitting `--sweeps` also selects all ten sweeps. To select a subset:

```sh
./main_runtime --sweeps avg_memory,tao,swap_prob --avg-memories 6,10,14
```

Outputs are `main_runtime_raw.csv`, `main_runtime_summary.csv` and one
`Greedy_<sweep>_runtime.ans` per selected sweep. ANS values are median runtime
in seconds, in the original fixed seven-algorithm column order. Existing files
with those names are replaced when the benchmark is run. Use a separate,
existing `--output-dir` to retain previous measurements:

```sh
mkdir -p ../data/ans/runtime_all
./main_runtime --sweeps all --output-dir ../data/ans/runtime_all
```

`ChartGenerator.py` already accepts the ten `Greedy_*_runtime.ans` names;
its ordinary data directory is `data/ans`.

For development without Gurobi, compile `main_time` and select available
algorithms explicitly. For example:

```sh
make main_time
mkdir -p ../data/ans/runtime_probe
./main_time --algorithms ZFA2 --sweeps all --instances 1 --repetitions 1 --warmups 0 --output-dir ../data/ans/runtime_probe
```

This produces partial algorithm columns for diagnostics only; it is not the
fixed seven-algorithm publication dataset. The formal EFiRAP baselines require Gurobi.

## Workload and timing

- Only the algorithm's `run()` is timed. Graph generation/loading, request
  generation, candidate path generation, constructors and CSV/ANS writing
  remain outside the timed region. Candidate paths always use the existing Greedy generator.
- Physical parameter sweeps reuse the same seeded graph and request pool.
- Memory-variation and memory-distribution sweeps generate their own input
  files while retaining the baseline requests. Memory distributions have the
  same total budget of 1000. Memory offsets use a separate generator RNG.
- Topology sweeps generate each topology and a seeded stratified request pool
  appropriate to that topology, matching the main experiment's policy.
- Exact-hop sweeps use SD pairs at the specified shortest-path distance,
  shuffled with the instance seed and repeated 3--6 times, following the main
  experiment's sampling policy with reproducible RNG instead of random_device.
  If no such pairs exist the benchmark fails rather than recording a different
  hop distance.
- New raw CSV fields record tao, swap_prob, hop_count, mem_vary, topo_vary,
  and mem_distribution. A mem_distribution value of -1 denotes the
  legacy heterogeneous input rather than the dedicated distribution sweep.
- `--reuse-inputs` also requires the suffixed topology/memory variant files.
  Generate them once with the same seed and options before reusing them.

