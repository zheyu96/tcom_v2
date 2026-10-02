"""Build a differential benchmark against the implementation before optimization."""
import argparse
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--reference", default="0bf285d2cfa677e500e4a6cf76aeb5d3e602c9ad")
args = parser.parse_args()
root = Path(__file__).resolve().parent
reference = root / ".compile_check/fnpr_flto_baseline"
reference.mkdir(parents=True, exist_ok=True)
for name in ("MyAlgo1", "MyAlgo3"):
    for extension in ("h", "cpp"):
        text = subprocess.check_output(
            ["git", "show", f"{args.reference}:src/Algorithm/{name}/{name}.{extension}"],
            cwd=root, encoding="utf-8",
        )
        text = text.replace(name, "Baseline" + name)
        text = text.replace("../AlgorithmBase/AlgorithmBase.h", "../../Algorithm/AlgorithmBase/AlgorithmBase.h")
        text = text.replace("__MYALGO1_H", "__BASELINE_MYALGO1_H").replace("__MYALGO3_H", "__BASELINE_MYALGO3_H")
        (reference / f"Baseline{name}.{extension}").write_text(text, encoding="utf-8")
sources = [
    "benchmark_fnpr_flto.cpp", "ExperimentWorkload.cpp", "config.cpp",
    "Network/Node/Node.cpp", "Network/Shape/Shape.cpp", "Network/Graph/Graph.cpp",
    "Network/PathMethod/PathMethodBase/PathMethod.cpp", "Network/PathMethod/Greedy/Greedy.cpp",
    "Algorithm/AlgorithmBase/AlgorithmBase.cpp",
    "Algorithm/WernerAlgo/WernerAlgo.cpp", "Algorithm/WernerAlgo2/WernerAlgo2.cpp",
    "Algorithm/WernerAlgo3/WernerAlgo3.cpp", "Algorithm/MyAlgo1/MyAlgo1.cpp",
    "Algorithm/MyAlgo3/MyAlgo3.cpp", "Algorithm/EFiRAP/EFiRAP.cpp",
    "Algorithm/EFiRAP_longtime/EFiRAP_longtime.cpp",
    ".compile_check/fnpr_flto_baseline/BaselineMyAlgo1.cpp",
    ".compile_check/fnpr_flto_baseline/BaselineMyAlgo3.cpp",
]
compiler = shutil.which("g++")
if compiler is None:
    raise SystemExit("g++ is required")
subprocess.run([compiler, "-O3", "-fopenmp", "-march=native", "-std=c++17", *sources,
                "-o", "benchmark_fnpr_flto.exe"], cwd=root, check=True)
print("Built benchmark_fnpr_flto.exe; run from src with main_time workload options.")
