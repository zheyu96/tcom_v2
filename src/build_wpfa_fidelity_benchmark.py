"""Build an exact before/after WPFA fidelity-threshold benchmark."""
import argparse
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--reference", default="51fd5ae0435a27ab440a69d515a70741315b3cf0")
args = parser.parse_args()
root = Path(__file__).resolve().parent
reference = root / ".compile_check/wpfa_fidelity_reference"
reference.mkdir(parents=True, exist_ok=True)
for name in ("WernerAlgo2",):
    for extension in ("h", "cpp"):
        text = subprocess.check_output(
            ["git", "show", f"{args.reference}:src/Algorithm/{name}/{name}.{extension}"],
            cwd=root, encoding="utf-8",
        )
        text = text.replace(name, "Before" + name)
        text = text.replace("../AlgorithmBase/AlgorithmBase.h", "../../Algorithm/AlgorithmBase/AlgorithmBase.h")
        text = text.replace("__WERNER_ALGO_H", "__BEFORE_WERNER_ALGO_H").replace("__WERNER_ALGO2_H", "__BEFORE_WERNER_ALGO2_H")
        (reference / f"Before{name}.{extension}").write_text(text, encoding="utf-8")
sources = [
    "benchmark_wpfa_fidelity.cpp", "ExperimentWorkload.cpp", "config.cpp",
    "Network/Node/Node.cpp", "Network/Shape/Shape.cpp", "Network/Graph/Graph.cpp",
    "Network/PathMethod/PathMethodBase/PathMethod.cpp", "Network/PathMethod/Greedy/Greedy.cpp",
    "Algorithm/AlgorithmBase/AlgorithmBase.cpp",
    "Algorithm/WernerAlgo/WernerAlgo.cpp", "Algorithm/WernerAlgo2/WernerAlgo2.cpp",
    "Algorithm/WernerAlgo3/WernerAlgo3.cpp", "Algorithm/MyAlgo1/MyAlgo1.cpp",
    "Algorithm/MyAlgo3/MyAlgo3.cpp", "Algorithm/EFiRAP/EFiRAP.cpp",
    "Algorithm/EFiRAP_longtime/EFiRAP_longtime.cpp",
]
sources += [".compile_check/wpfa_fidelity_reference/BeforeWernerAlgo2.cpp"]
executable = "benchmark_wpfa_fidelity.exe"
compiler = shutil.which("g++")
if compiler is None:
    raise SystemExit("g++ is required")
subprocess.run([compiler, "-O3", "-fopenmp", "-march=native", "-std=c++17", *sources,
                "-o", executable], cwd=root, check=True)
print(f"Built {executable}; run from src with main_time workload options.")
