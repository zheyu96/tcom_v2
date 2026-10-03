"""Build the shared Werner solver feasibility and before/after quality check."""
import argparse
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--reference", default="f6c2c31deacfd2f8b078f7071ecdc6cc577eaf63")
parser.add_argument("--runtime", action="store_true", help="Build the shared runtime driver instead of the quality check")
args = parser.parse_args()
root = Path(__file__).resolve().parent
reference = root / ".compile_check/werner_before_alignment"
reference.mkdir(parents=True, exist_ok=True)
for name in (() if args.runtime else ("WernerAlgo", "WernerAlgo2")):
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
    ("main_time.cpp" if args.runtime else "check_werner_alignment.cpp"), "ExperimentWorkload.cpp", "config.cpp",
    "Network/Node/Node.cpp", "Network/Shape/Shape.cpp", "Network/Graph/Graph.cpp",
    "Network/PathMethod/PathMethodBase/PathMethod.cpp", "Network/PathMethod/Greedy/Greedy.cpp",
    "Algorithm/AlgorithmBase/AlgorithmBase.cpp",
    "Algorithm/WernerAlgo/WernerAlgo.cpp", "Algorithm/WernerAlgo2/WernerAlgo2.cpp",
    "Algorithm/WernerAlgo3/WernerAlgo3.cpp", "Algorithm/MyAlgo1/MyAlgo1.cpp",
    "Algorithm/MyAlgo3/MyAlgo3.cpp", "Algorithm/EFiRAP/EFiRAP.cpp",
    "Algorithm/EFiRAP_longtime/EFiRAP_longtime.cpp",
]
if not args.runtime:
    sources += [".compile_check/werner_before_alignment/BeforeWernerAlgo.cpp",
                ".compile_check/werner_before_alignment/BeforeWernerAlgo2.cpp"]
executable = "runtime_aligned_probe.exe" if args.runtime else "check_werner_alignment.exe"
compiler = shutil.which("g++")
if compiler is None:
    raise SystemExit("g++ is required")
subprocess.run([compiler, "-O3", "-fopenmp", "-march=native", "-std=c++17", *sources,
                "-o", executable], cwd=root, check=True)
print(f"Built {executable}; run from src with main_time workload options.")
