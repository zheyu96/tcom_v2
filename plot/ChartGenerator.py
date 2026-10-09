#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import math
import sys
import csv
import argparse
import numpy as np

import matplotlib
matplotlib.use("Agg")  # 無頭後端（WSL/遠端/CI）安全
import matplotlib.pyplot as plt
from matplotlib.ticker import MultipleLocator, FuncFormatter, FixedLocator
from matplotlib import font_manager as fm


# =========================
# 強制使用 Times New Roman
# =========================
def _require_times_new_roman():
    """
    嚴格要求使用 Times New Roman。
    """
    candidates = []
    env_paths = os.environ.get("TNR_PATHS", "")
    if env_paths:
        for p in env_paths.split(","):
            p = p.strip()
            if p:
                candidates.append(p)

    candidates += [
        r"C:\Windows\Fonts\times.ttf",
        r"C:\Windows\Fonts\timesi.ttf",
        r"C:\Windows\Fonts\timesbd.ttf",
        r"C:\Windows\Fonts\timesbi.ttf",
        r"C:\Windows\Fonts\Times New Roman.ttf",
        r"C:\Windows\Fonts\Times New Roman Italic.ttf",
        r"C:\Windows\Fonts\Times New Roman Bold.ttf",
        r"C:\Windows\Fonts\Times New Roman Bold Italic.ttf",
    ]

    candidates += [
        "/System/Library/Fonts/Times.ttc",
        "/Library/Fonts/Times New Roman.ttf",
        "/Library/Fonts/Times New Roman Italic.ttf",
        "/Library/Fonts/Times New Roman Bold.ttf",
        "/Library/Fonts/Times New Roman Bold Italic.ttf",
    ]

    candidates += [
        "/usr/share/fonts/truetype/msttcorefonts/times.ttf",
        "/usr/share/fonts/truetype/msttcorefonts/Times_New_Roman.ttf",
        "/usr/share/fonts/truetype/msttcorefonts/Times_New_Roman/Times_New_Roman.ttf",
        "/usr/share/fonts/truetype/msttcorefonts/Times_New_Roman.ttf",
    ]

    for path in candidates:
        try:
            if os.path.isdir(path):
                for fpath in fm.findSystemFonts(fontpaths=[path]):
                    fm.fontManager.addfont(fpath)
            elif os.path.isfile(path):
                fm.fontManager.addfont(path)
        except Exception:
            pass

    matplotlib.rcParams.update({
        "font.family": "Times New Roman",
        "pdf.fonttype": 42,
        "ps.fonttype": 3,
        "axes.unicode_minus": False,
        "mathtext.fontset": "cm",
        "text.usetex": False,
        "xtick.labelsize": 25,
        "ytick.labelsize": 25,
        "axes.labelsize": 25,
        "axes.titlesize": 25,
    })

    try:
        _ = fm.findfont("Times New Roman", fontext="ttf", fallback_to_default=False)
    except TypeError:
        has_tnr = False
        for fpath in fm.findSystemFonts():
            try:
                f = fm.get_font(fpath)
                name = f.family_name
                if name and "Times New Roman" in name:
                    has_tnr = True
                    break
            except Exception:
                pass
        if not has_tnr:
            _fail_tnr()
        else:
            return
    except Exception:
        _fail_tnr()

def _fail_tnr():
    msg = (
        "\n[ERROR] Times New Roman 未安裝或未被 Matplotlib 偵測到。\n"
        "請安裝字型後再執行。\n"
    )
    print(msg, file=sys.stderr)
    raise SystemExit(2)


class ChartGenerator:
    """
    用法: ChartGenerator(dataName, Xkey, Ykey, label_every=None)
    """

    # Outer export margin in inches and label spacing in points.
    _SHARED_LEGEND_HANDLES = {}
    _SMALL_SCALE_LEGEND_HANDLES = {}
    _BAR_LEGEND_HANDLES = {}
    _EXPORT_PAD_INCHES = 0.12
    _AXIS_LABEL_PAD = 6
    _TICK_LABEL_PAD = 5

    # Font sizes in points; edit bar and line charts independently.
    # Categorical X tick overrides accommodate longer, multiline labels.
    _BAR_FONT_SIZES = {
        "axis_label": 32,
        "x_tick": 20,
        "y_tick": 32,
        "legend": 20,
        "x_tick_overrides": {},
    }
    _LINE_FONT_SIZES = {
        "axis_label": 40,
        "x_tick": 40,
        "y_tick": 40,
        "legend": 20,
        "x_tick_overrides": {},
    }

    _RIGHT_TOP  = (0.55, 0.83)
    _LEFT_TOP   = (0.48, 0.83)
    _RIGHT_DOWN = (0.55, 0.13)
    _LEFT_DOWN  = (0.40, 0.13)

    _BBOX_POS = {
        'fidelity_gain':{
            'request_cnt': _LEFT_TOP, 'tao': _RIGHT_TOP, 'time_limit': _LEFT_TOP,
            'avg_memory': _LEFT_TOP, 'mem_vary': _LEFT_TOP,
            'topo_vary': _LEFT_TOP, 'mem_distribution': _LEFT_TOP,
            'path_set': _LEFT_TOP,
            'min_fidelity': _LEFT_TOP, 'fidelity_threshold': _RIGHT_TOP,
            'swap_prob': _LEFT_TOP, 'entangle_time': _RIGHT_DOWN, 'entangle_prob': _LEFT_DOWN,
            'hop_count': _RIGHT_TOP,
            'Zmin': _LEFT_TOP, 'time_eta': _LEFT_TOP, 'bucket_eps': _LEFT_TOP
        },
        'succ_request_cnt':{
            'request_cnt': _LEFT_TOP, 'tao': _RIGHT_TOP, 'time_limit': _LEFT_TOP,
            'avg_memory': _LEFT_TOP, 'mem_vary': _LEFT_TOP,
            'topo_vary': _LEFT_TOP, 'mem_distribution': _LEFT_TOP,
            'path_set': _LEFT_TOP,
            'min_fidelity': _LEFT_TOP, 'fidelity_threshold': _RIGHT_TOP,
            'swap_prob': _LEFT_TOP, 'entangle_time': _RIGHT_DOWN, 'entangle_prob': _LEFT_DOWN,
            'hop_count': _RIGHT_TOP,
            'Zmin': _LEFT_TOP, 'time_eta': _LEFT_TOP, 'bucket_eps': _LEFT_TOP
        },
        'pure_fidelity':{
            'request_cnt': _LEFT_TOP, 'tao': _RIGHT_TOP, 'time_limit': _LEFT_TOP,
            'avg_memory': _LEFT_TOP, 'mem_vary': _LEFT_TOP,
            'topo_vary': _LEFT_TOP, 'mem_distribution': _LEFT_TOP,
            'path_set': _LEFT_TOP,
            'min_fidelity': _LEFT_TOP, 'fidelity_threshold': _RIGHT_TOP,
            'swap_prob': _LEFT_TOP, 'entangle_time': _RIGHT_DOWN, 'entangle_prob': _LEFT_DOWN,
            'Zmin': _LEFT_TOP, 'time_eta': _LEFT_TOP, 'bucket_eps': _LEFT_TOP,
            'hop_count': "auto",
        },
        'actual_req_cnt':{
            'request_cnt': _LEFT_TOP, 'tao': _RIGHT_TOP, 'time_limit': _LEFT_TOP,
            'avg_memory': _LEFT_TOP, 'mem_vary': _LEFT_TOP,
            'topo_vary': _LEFT_TOP, 'mem_distribution': _LEFT_TOP,
            'path_set': _LEFT_TOP,
            'min_fidelity': _LEFT_TOP, 'fidelity_threshold': _RIGHT_TOP,
            'swap_prob': _LEFT_TOP, 'entangle_time': _RIGHT_DOWN, 'entangle_prob': _LEFT_DOWN,
            'hop_count': _RIGHT_TOP,
            'Zmin': _LEFT_TOP, 'time_eta': _LEFT_TOP, 'bucket_eps': _LEFT_TOP
        },
        'runtime':{
            'request_cnt': _LEFT_TOP, 'tao': _RIGHT_TOP, 'time_limit': _LEFT_TOP,
            'avg_memory': _LEFT_TOP, 'mem_vary': _LEFT_TOP,
            'topo_vary': _LEFT_TOP, 'mem_distribution': _LEFT_TOP,
            'path_set': _LEFT_TOP,
            'min_fidelity': _LEFT_TOP, 'fidelity_threshold': _RIGHT_TOP,
            'swap_prob': _LEFT_TOP, 'entangle_time': _RIGHT_DOWN, 'entangle_prob': _LEFT_DOWN,
            'hop_count': _RIGHT_TOP,
            'Zmin': _LEFT_TOP, 'time_eta': _LEFT_TOP, 'bucket_eps': _LEFT_TOP
        },
    }

    # Tuple: (minimum, maximum, minor spacing, label every N ticks).
    # List: explicit labeled ticks, with first/last values as axis limits.
    # Example: [50, 65, 80, 95]. _CUSTOM_Y_TICKS can override the labels.
    _Y_INTERVALS = {
        'fidelity_gain':{
            'request_cnt': (20, 100, 5, 4),
            'tao': (0, 80, 5, 4), 'time_limit': (30, 70, 5, 2),
            'avg_memory': (20, 70, 5, 2), 'mem_vary': (30,70,5,2),
            'topo_vary': (0,75,5,5), 'mem_distribution': (0,75,5,5),
            'path_set': (0,75,5,5),
            'min_fidelity': (23, 63, 5, 1), 'fidelity_threshold': (0, 80, 5, 4),
            'swap_prob': (10, 80, 5, 4), 'entangle_time': "auto",
            'hop_count':(10,90,5,3),
            'entangle_prob': "auto", 'Zmin': "auto", 'time_eta': "auto", 'bucket_eps': "auto"
        },
        'succ_request_cnt':{
            'request_cnt': (30, 140, 5, 6),
            'tao': (0, 90, 5, 6), 'time_limit': (40, 90, 5, 4),
            'avg_memory': (30, 90, 5, 6), 'mem_vary': "auto",
            'topo_vary': "auto", 'mem_distribution': "auto",
            'path_set': "auto",
            'min_fidelity': (65, 95, 5, 1), 'fidelity_threshold': (0, 105, 5, 5),
            'swap_prob': (20, 90, 5, 4), 'hop_count':(20,105,5,7),
            'entangle_time': "auto",
            'entangle_prob': "auto", 'Zmin': "auto", 'time_eta': "auto", 'bucket_eps': "auto"
        },
        'pure_fidelity':{
            'request_cnt': (50, 80, 5, 2), 'tao': (50, 70, 1, 5),
            'time_limit': (45, 70, 1, 5), 'avg_memory': (20, 40, 1, 5),
            'mem_vary': "auto", 'topo_vary': "auto", 'mem_distribution': "auto",
            'path_set': "auto",
            'min_fidelity': (50, 80, 1, 5), 'fidelity_threshold': (50, 70, 1, 5),
            'swap_prob': "auto", 'entangle_time': "auto", 'entangle_prob': "auto",
            'hop_count': "auto",
            'Zmin': "auto", 'time_eta': "auto", 'bucket_eps': "auto"
        },
        'actual_req_cnt':{
            'request_cnt': (40,140,5,4),
            'tao': [0,25,50,75,99], 'time_limit': [50,65,80,95],
            'avg_memory': (40,100,5,4), 'mem_vary': [40,55,80,95],
            'topo_vary': (0,105,5,5), 'mem_distribution': (0,105,5,5),
            'path_set': (0,105,5,5),
            'min_fidelity': "auto", 'fidelity_threshold': (0,105,5,7),
            'swap_prob': (50,100,5,5), 'hop_count': "auto",
            'entangle_time': "auto",
            'entangle_prob': "auto", 'Zmin': "auto", 'time_eta': "auto", 'bucket_eps': "auto"
        },
        'runtime':{
            'request_cnt': (0,5,1,1), 'tao': "auto", 'time_limit': (0,5,1,1),
            'avg_memory': (0,5,1,1), 'mem_vary': "auto", 'topo_vary': "auto",
            'mem_distribution': "auto", 'path_set': "auto",
            'min_fidelity': "auto", 'fidelity_threshold': (0,5,1,1),
            'swap_prob': "auto", 'hop_count': "auto", 'entangle_time': "auto",
            'entangle_prob': "auto", 'Zmin': "auto", 'time_eta': "auto", 'bucket_eps': "auto"
        },
    }

    # Manual Y-axis controls used only by SmallScale_*.ans figures.
    #
    # Tuple format:
    #   (minimum, maximum, minor_tick_interval, major_label_every_n_ticks)
    # Use "auto" for any individual figure to restore automatic scaling.
    # Runtime values below are in seconds even when the axis displays x10^-3.
    _SMALL_SCALE_Y_INTERVALS = {
        'fidelity_gain': {
            'request_cnt': (1, 5, 0.5, 2),
            'fidelity_threshold': (1, 4.0, 0.5, 2),
            'tao': (2, 4, 0.25, 4),
            'swap_prob': (2, 4, 0.25, 4),
            'time_limit': (1,4,0.25,4),
            'avg_memory': (1, 4.0, 0.5, 2),
        },
        'succ_request_cnt': {
            'request_cnt': (0, 6, 0.5, 2),
            'fidelity_threshold': (1.5, 4.5, 0.25, 2),
            'tao': (1.5, 4.5, 0.25, 2),
            'swap_prob': (1.5, 4.5, 0.25, 2),
            'time_limit': "auto",
            'avg_memory': (0, 4.5, 0.5, 2),
        },
        'actual_req_cnt': {
            'request_cnt': (0, 6, 0.5, 2),
            'fidelity_threshold': (0, 5, 0.5, 2),
            'tao': (0, 5, 0.5, 2),
            'swap_prob': (0, 5, 0.5, 2),
            'time_limit': "auto",
            'avg_memory': (0, 5, 0.5, 2),
        },
        'runtime': {
            'request_cnt': (0, 0.010, 0.001, 1),
            'fidelity_threshold': (0, 0.010, 0.001, 2),
            'tao': (0, 0.010, 0.001, 2),
            'swap_prob': (0, 0.010, 0.001, 2),
            'time_limit': "auto",
            'avg_memory': (0, 0.010, 0.001, 2),
        },
    }

    # Optional major Y ticks, keyed by dataset -> Y key -> X key.
    # Missing entries or None keep the existing tick behavior unchanged.
    # Values use ORIGINAL data units, before the automatic 10^n scaling.
    # Only labeled major ticks are replaced; ranges and minor ticks stay as set.
    # Each list must be strictly increasing and within the configured Y range.
    _CUSTOM_Y_TICKS = {
        "Greedy": {
            # "fidelity_gain": {"request_cnt": [20, 40, 75, 100, 140]},
        },
        "SmallScale": {
            # "fidelity_gain": {"request_cnt": [1, 2, 3, 4, 5]},
            # "runtime": {"request_cnt": [0, 0.002, 0.005, 0.010]},
        },
    }

    _AXIS_NAME = {
        "request_cnt": r"$\#$Requests", "time_limit": "$| T |$",
        "avg_memory": "Average Mem. Capacity", "mem_vary": "Memory Variation",
        "topo_vary": "Topology Model", "topology_vary": "Topology Model",
        "mem_distribution": "Memory Distribution",
        "path_set": "Path Set",
        "tao": r"$\it{\delta}$",
        "swap_prob": "Swapping Probability", "fidelity_gain": "Exp. W. Param. Sum",
        "succ_request_cnt": r"$\#$Exp. Accepted Requests", "pure_fidelity": "Average Final Fidelity",
        "actual_req_cnt": r"$\#$Acc. Requests",
        "runtime": "Runtime (s)",
        "fidelity_threshold": "Fidelity Threshold", "min_fidelity": "Minimum Initial Fidelity",
        "entangle_time": "Entangling Time", "entangle_prob": "Entangling Probability",
        "Zmin": r"$Z_{min}$", "time_eta": r"$\eta$", "bucket_eps": r"bucket $\epsilon$",
        "hop_count": "Hop Threshold",
    }

    _ONLY_WERNER_X = {"Zmin", "time_eta", "bucket_eps"}

    # 欄位順序與 main.cpp 相同：
    # ZFA_UB=WernerAlgo3, ZFA=WernerAlgo, ZFA2=WernerAlgo2,
    # MyAlgo1=MyAlgo1, MyAlgo3=MyAlgo3, EFiRAP=EFiRAP,
    # EFiRAP_longtime=EFiRAP-long
    # Column order written by main.cpp's algo_names:
    # ZFA_UB -> UB, ZFA -> WPFA-noPurify, ZFA2 -> WPFA,
    # MyAlgo1 -> FNPR, MyAlgo3 -> FLTO, EFiRAP -> EFiRAP,
    # EFiRAP_longtime -> EFiRAP-long.
    _ALGO_NAMES = [
        "UB", "WPFA-noPurify", "WPFA", "FNPR", "FLTO", "EFiRAP",
        "EFiRAP-long",
    ]
    _RAW_ALGO_ORDER = [
        "ZFA_UB", "ZFA", "ZFA2", "MyAlgo1", "MyAlgo3", "EFiRAP",
        "EFiRAP_longtime",
    ]
    _RUNTIME_RAW_SWEEPS = {
        "request_cnt", "time_limit", "fidelity_threshold",
    }

    # 保持資料欄位順序，EFiRAP-long 是 main.cpp 輸出的最後一個演算法。
    _DRAW_ORDER = [0, 1, 2, 3, 4, 5, 6]

    # 保持原本各欄位使用的 marker 與顏色。
    _MARKERS = ['s', '*', 's', 'v', 'o', '^', 'x', '1', 'D']
    _COLORS = ["#800080", "#FF0000", "#0000FF", "#01C501", "#800080", "#FF00FF", "#00FFFF", "#808080", "#555555"]
    # Softer colors for categorical bar charts.  Keep the black outlines and
    # hatches below so adjacent algorithms remain easy to distinguish in
    # grayscale printouts as well.
    _PASTEL_BAR_COLORS = [
        "#CDB4DB",  # UB: soft lavender
        "#FFADAD",  # WPFA-noPurify: soft red
        "#A0C4FF",  # WPFA: soft blue
        "#CAFFBF",  # FNPR: soft green
        "#BDB2FF",  # FLTO: lavender blue
        "#FFC6FF",  # EFiRAP: soft pink
        "#9BF6FF",  # EFiRAP-long: soft cyan
    ]
    _HATCHES = ['', '//', '\\\\', 'xx', '..', '++', 'oo', '--', '**']

    # Use algorithm names rather than data-column positions for bar styles.
    # Both the plot and its shared legend use these exact patch properties.
    _BAR_STYLES = {
        "UB": {"facecolor": "#CDB4DB", "hatch": ""},
        "OPT": {"facecolor": "#CDB4DB", "hatch": ""},
        "WPFA-noPurify": {"facecolor": "#FFADAD", "hatch": "//"},
        "WPFA": {"facecolor": "#A0C4FF", "hatch": "\\\\"},
        "FNPR": {"facecolor": "#CAFFBF", "hatch": "xx"},
        "FLTO": {"facecolor": "#BDB2FF", "hatch": ".."},
        "EFiRAP": {"facecolor": "#FFC6FF", "hatch": "++"},
        "EFiRAP-long": {"facecolor": "#9BF6FF", "hatch": "oo"},
    }
    _BAR_EDGE_STYLE = {"edgecolor": "black", "linewidth": 0.8}

    _SKIP_ALGOS = ["Nesting", "Linear", "ASAP"]

    def __init__(self, dataName: str, x_key: str, y_key: str, label_every: int|None = None,
                 legend_mode="separate", legend_columns=1):
        if legend_mode not in ("separate", "inline") or legend_columns < 1:
            raise ValueError("Invalid legend mode or column count")
        path = os.path.join('..', 'data', 'ans', dataName)
        runtime_raw_path = os.path.join(
            '..', 'data', 'ans', 'main_runtime_raw.csv'
        )
        is_small_scale = os.path.basename(dataName).startswith("SmallScale_")
        use_runtime_raw = (
            not is_small_scale
            and os.path.basename(dataName).startswith("Greedy_")
            and y_key == "runtime"
            and x_key in self._RUNTIME_RAW_SWEEPS
            and os.path.exists(runtime_raw_path)
        )
        if not use_runtime_raw and not os.path.exists(path):
            print(f"[WARN] file doesn't exist: {os.path.abspath(path)}")
            return

        if is_small_scale:
            # main_smallscale writes exactly two columns in this order.
            self._ALGO_NAMES = ["OPT", "WPFA"]
            self._DRAW_ORDER = [0, 1]
            # OPT and WPFA are often numerically identical on the tiny test
            # cases.  A large hollow square below a blue cross keeps both
            # series visible even when their coordinates coincide exactly.
            self._MARKERS = ['s', 'x']
            self._COLORS = ["#800080", "#0000FF"]
            self._HATCHES = ['', '\\\\']
            self._LINESTYLES = ['--', '-']
            self._LINEWIDTHS = [1.8, 2.2]
            self._MARKER_SIZES = [17, 13]
            self._MARKER_EDGE_WIDTHS = [2.5, 3.0]
            self._PLOT_ZORDERS = [2, 4]
        else:
            self._LINESTYLES = ['-'] * len(self._ALGO_NAMES)
            self._LINEWIDTHS = [1.0] * len(self._ALGO_NAMES)
            self._MARKER_SIZES = [16] * len(self._ALGO_NAMES)
            self._MARKER_EDGE_WIDTHS = [2.5] * len(self._ALGO_NAMES)
            self._PLOT_ZORDERS = [
                -index for index in range(len(self._ALGO_NAMES))
            ]

        if use_runtime_raw:
            try:
                x_vals, y = self._load_runtime_raw_csv(
                    runtime_raw_path, x_key
                )
            except (OSError, ValueError) as error:
                print(f"[WARN] cannot load runtime raw CSV: {error}")
                return
            num_rows = len(x_vals)
            num_algos = len(y)
            print(
                "start generate", runtime_raw_path,
                f"(median aggregation for {x_key})",
            )
        else:
            with open(path, 'r', encoding='utf-8') as f:
                raw_lines = f.readlines()
            print("start generate", path)

            x_vals, y_vals = [], []
            num_rows, num_cols = 0, None
            for line in raw_lines:
                line = line.strip()
                if not line:
                    continue
                parts = [p for p in line.split() if p != ""]
                if len(parts) < 2:
                    continue
                if num_cols is None:
                    num_cols = len(parts)
                if len(parts) != num_cols:
                    continue

                num_rows += 1
                x_vals.append(parts[0])
                good = True
                for i in range(1, len(parts)):
                    try:
                        y_vals.append(float(parts[i]))
                    except ValueError:
                        good = False
                        break
                if not good:
                    num_rows -= 1
                    x_vals.pop()
                    y_vals = y_vals[:(len(y_vals) - (len(parts)-1))]

            if num_rows == 0 or num_cols is None or num_cols < 2:
                print("[WARN] no valid data rows.")
                return

            num_algos = num_cols - 1
            if num_algos > len(self._ALGO_NAMES):
                print(
                    f"[WARN] data has {num_algos} algorithm columns, but only "
                    f"{len(self._ALGO_NAMES)} names are configured."
                )
                return

            y = np.array(y_vals).reshape(num_rows, num_algos).T.tolist()

        is_categorical_bar = x_key in (
            "topo_vary", "topology_vary", "mem_distribution", "path_set"
        )

        def should_draw_algorithm(i: int) -> bool:
            name = self._ALGO_NAMES[i]
            if name in self._SKIP_ALGOS or (is_categorical_bar and name == "UB"):
                return False
            if x_key in self._ONLY_WERNER_X and name != "G-Werner":
                return False
            # UB is not a schedulable algorithm, so its request counts and
            # execution time are not part of those comparison figures.
            if y_key in ("succ_request_cnt", "actual_req_cnt", "runtime") and name == "UB":
                return False
            return True

        visible_indices = [i for i in range(num_algos) if should_draw_algorithm(i)]
        if not visible_indices:
            print(f"[WARN] no algorithms selected for x={x_key}, y={y_key}.")
            return

        # X 軸處理
        Xpow = -3 if x_key in ("tao", "entangle_time", "entangle_prob") else 0
        if Xpow != 0:
            tmp = []
            for xv in x_vals:
                try:
                    tmp.append(float(xv) / (10 ** Xpow))
                except ValueError:
                    tmp.append(xv)
            x_labels = [f"{v:g}" if isinstance(v, float) else str(v) for v in tmp]
        else:
            x_labels = [str(v) for v in x_vals]

        if is_small_scale:
            # Keep the compact few-point SmallScale axes readable.  The ANS
            # files use full double precision, which is useful for replay but
            # far too verbose for tick labels.
            if x_key in ("request_cnt", "time_limit", "avg_memory"):
                x_labels = [
                    f"{int(round(float(value)))}" for value in x_vals
                ]
            elif x_key in ("fidelity_threshold", "swap_prob"):
                x_labels = [f"{float(value):.2f}" for value in x_vals]
            elif x_key == "tao":
                x_labels = [
                    f"{float(value) * 1000.0:.1f}" for value in x_vals
                ]

        if x_key == "mem_vary":
            varied_labels = []
            for value in x_vals:
                try:
                    varied_labels.append(rf"$\pm {float(value):g}$")
                except ValueError:
                    varied_labels.append(str(value))
            x_labels = varied_labels

        if x_key in ("topo_vary", "topology_vary"):
            topology_labels = {0: "Waxman", 1: "Grid", 2: "RGG"}
            x_labels = [
                topology_labels.get(int(float(value)), str(value))
                for value in x_vals
            ]

        if x_key == "mem_distribution":
            distribution_labels = {
                0: "Fixed",
                1: "Degree-\nProportional",
                2: "Inverse-\nDegree",
                3: "Uniform",
            }
            x_labels = [
                distribution_labels.get(int(float(value)), str(value))
                for value in x_vals
            ]

        if x_key == "path_set":
            path_set_labels = {
                0: "K-Shortest\nPath",
                1: "QCAST",
                2: "REPS",
            }
            x_labels = [
                path_set_labels.get(int(float(value)), str(value))
                for value in x_vals
            ]

        # Y 軸處理
        raw_max = max(max(y[i]) for i in visible_indices)
        # Runtime ANS/CSV values are seconds; keep that unit on every chart.
        Ypow = 0 if y_key == "runtime" else self._pick_engineering_exp(abs(raw_max))
        Ydiv = float(10 ** Ypow) if Ypow != 0 else 1.0

        max_data, min_data = -math.inf, math.inf
        for i in range(num_algos):
            for j in range(num_rows):
                y[i][j] = y[i][j] / Ydiv
                if i in visible_indices:
                    max_data = max(max_data, y[i][j])
                    min_data = min(min_data, y[i][j])

        # 畫圖
        if is_categorical_bar:
            # Fixed canvas and axes bounds keep all categorical panels aligned.
            fig = plt.figure(figsize=(6.8, 5.4), dpi=600)
            ax1 = fig.add_axes([0.25, 0.30, 0.72, 0.66])
        else:
            fig, ax1 = plt.subplots(figsize=(6.8, 5.4), dpi=600, constrained_layout=True)
        ax1.tick_params(direction="in", bottom=True, top=True, left=True, right=True, pad=self._TICK_LABEL_PAD)

        x_positions = np.arange(len(x_labels), dtype=float)
        categorical_bar_count = len([
            i for i in self._DRAW_ORDER
            if i < num_algos and should_draw_algorithm(i)
        ])
        categorical_bar_index = 0
        plotted = []
        for draw_idx in range(min(10, num_algos)):
            if draw_idx >= len(self._DRAW_ORDER):
                break
            i = self._DRAW_ORDER[draw_idx]
            if i >= num_algos:
                continue

            if not should_draw_algorithm(i):
                continue

            if is_categorical_bar:
                # Equal physical bar widths for three- and four-category panels.
                group_width = 0.82 * len(x_labels) / 4.0
                bar_width = group_width / categorical_bar_count
                offset = (
                    categorical_bar_index - (categorical_bar_count - 1) / 2.0
                ) * bar_width
                ax1.bar(
                    x_positions + offset, y[i],
                    width=bar_width * 0.9,
                    **self._BAR_STYLES[self._ALGO_NAMES[i]],
                    **self._BAR_EDGE_STYLE,
                    zorder=2, label=self._ALGO_NAMES[i],
                )
                categorical_bar_index += 1
            else:
                ax1.plot(
                    x_positions, y[i],
                    color=self._COLORS[i],
                    lw=self._LINEWIDTHS[i], ls=self._LINESTYLES[i],
                    marker=self._MARKERS[i],
                    markersize=self._MARKER_SIZES[i],
                    markerfacecolor='None',
                    markeredgewidth=self._MARKER_EDGE_WIDTHS[i],
                    zorder=self._PLOT_ZORDERS[i], label=self._ALGO_NAMES[i],
                )
            plotted.append(i)

        legend_handles, legend_names = ax1.get_legend_handles_labels()
        font_sizes = self._BAR_FONT_SIZES if is_categorical_bar else self._LINE_FONT_SIZES
        if is_small_scale and x_key == "time_limit":
            # Fit the long vertical objective label in the existing canvas.
            font_sizes = {**font_sizes, "axis_label": 32}
        legend_options = dict(
            prop={"size": font_sizes["legend"], "family": "Times New Roman"},
            frameon=False, handletextpad=0.5, handlelength=1,
            labelspacing=0.2, columnspacing=0.6,
            ncol=legend_columns, facecolor='None',
        )
        if legend_mode == "inline":
            bbox_pos = self._BBOX_POS[y_key].get(x_key, (0.5, 0.5))
            ax1.legend(legend_handles, legend_names, loc='center',
                       bbox_to_anchor=bbox_pos, **legend_options)

        if is_small_scale:
            cfg = self._SMALL_SCALE_Y_INTERVALS.get(y_key, {}).get(x_key, "auto")
        else:
            cfg = self._Y_INTERVALS[y_key].get(x_key, "auto")
        # Lists specify explicit ticks; tuples retain the legacy range format.
        interval_ticks = cfg if isinstance(cfg, list) else None
        if interval_ticks is not None:
            if len(interval_ticks) < 2:
                raise ValueError("Y interval tick lists require at least two values")
            self._scaled_custom_y_ticks(
                interval_ticks, 1.0, float(interval_ticks[0]),
                float(interval_ticks[-1]), f"{dataName}: Y interval list",
            )
            cfg = (interval_ticks[0], interval_ticks[-1],
                   self._nice_step((interval_ticks[-1] - interval_ticks[0]) / 6), 1)
        manual_y_range = cfg != "auto"
        if cfg == "auto":
            range_min, range_max = min_data, max_data
            if is_small_scale and min_data >= 0 and max_data > 0:
                # A five-percent data-span pad makes almost-equal OPT/WPFA
                # values look misleadingly far apart.  Match the visual
                # breathing room of the original Greedy figures instead.
                range_min = 0.0 if y_key == "runtime" else 0.60 * min_data
                range_max = 1.20 * max_data
            Ystart, Yend, Yinterval = self._auto_y_range(
                range_min, range_max, target_ticks=7, padding=0.05,
            )
            span = max(1e-12, Yend - Ystart)
            rough_labels = span / max(1e-12, Yinterval)
            label_step_factor = max(1, int(math.ceil(rough_labels / 7)))
        else:
            if len(cfg) == 3:
                _Ys, _Ye, _Yi = cfg; _label = 1
            else:
                _Ys, _Ye, _Yi, _label = cfg
            Ystart, Yend, Yinterval = _Ys / Ydiv, _Ye / Ydiv, _Yi / Ydiv
            label_step_factor = int(max(1, _label))

        if is_categorical_bar and not manual_y_range:
            # Keep a zero baseline so the grouped bars do not exaggerate small
            # differences between categorical experiment settings.
            Ystart = min(0.0, Ystart)
            Yend = max(0.0, Yend)
            rough_labels = (Yend - Ystart) / max(1e-12, Yinterval)
            label_step_factor = max(
                label_step_factor, int(math.ceil(rough_labels / 7))
            )

        if label_every is not None:
            label_step_factor = int(max(1, label_every))

        if plotted and not manual_y_range:
            ymax_plotted = max(max(y[i]) for i in plotted)
            top_needed = (math.ceil(ymax_plotted / Yinterval) + 1) * Yinterval
            if top_needed > Yend:
                Yend = top_needed

        if is_small_scale and x_key == "tao":
            Xlabel_text = "Slot Duration (ms)"
        elif is_small_scale and x_key == "avg_memory":
            Xlabel_text = "Average Mem. Capacity"
        else:
            Xpow_disp_unit = " s" if x_key in ("tao", "entangle_time") else ""
            Xlabel_text = self._AXIS_NAME.get(x_key, x_key) + self._gen_power_suffix(Xpow, Xpow_disp_unit)
        Ylabel_text = self._AXIS_NAME.get(y_key, y_key) + self._gen_power_suffix(Ypow, "")
        if y_key == "actual_req_cnt":
            Ylabel_text = "Acc. Requests(x10)" + self._gen_power_suffix(Ypow, "")

        major_step = Yinterval * label_step_factor
        minor_step = Yinterval
        span = max(1e-12, Yend - Ystart)
        approx_labels = span / max(1e-12, major_step)
        if approx_labels > 1000:
            factor = max(1, math.ceil(approx_labels / 12))
            major_step *= factor

        ax1.set_ylim(Ystart, Yend)
        ax1.yaxis.set_major_locator(MultipleLocator(major_step))
        ax1.yaxis.set_minor_locator(MultipleLocator(minor_step))
        dataset_name = "SmallScale" if is_small_scale else "Greedy"
        custom_ticks = self._CUSTOM_Y_TICKS.get(dataset_name, {}).get(
            y_key, {}
        ).get(x_key)
        if custom_ticks is None:
            custom_ticks = interval_ticks
        if custom_ticks is not None:
            ax1.yaxis.set_major_locator(FixedLocator(self._scaled_custom_y_ticks(
                custom_ticks, Ydiv, Ystart, Yend,
                f"{dataset_name}/{y_key}/{x_key}",
            )))
        ax1.tick_params(axis='y', which='major', labelsize=font_sizes["y_tick"])
        ax1.tick_params(axis='y', which='minor', length=4, labelsize=0)

        def _fmt_y(val, pos):
            if y_key == "runtime":
                return np.format_float_positional(
                    0.0 if abs(val) < 1e-12 else val, precision=9, trim='-'
                )
            if y_key == "actual_req_cnt":
                return f"{val / 10:g}"
            if abs(val) >= 100 or abs(val) == int(val): return f"{val:.0f}"
            elif abs(val) >= 10: return f"{val:.1f}"
            else: return f"{val:.2f}"
        ax1.yaxis.set_major_formatter(FuncFormatter(_fmt_y))

        x_tick_fontsize = font_sizes["x_tick_overrides"].get(
            x_key, font_sizes["x_tick"]
        )

        tick_indices = list(range(len(x_labels)))
        if is_small_scale and len(tick_indices) > 5:
            tick_step = int(math.ceil(len(tick_indices) / 5.0))
            tick_indices = tick_indices[::tick_step]
            if tick_indices[-1] != len(x_labels) - 1:
                tick_indices.append(len(x_labels) - 1)
        plt.xticks(
            ticks=tick_indices,
            labels=[x_labels[index] for index in tick_indices],
            fontsize=x_tick_fontsize
        )
        if is_categorical_bar:
            ax1.set_xlim(-0.5, len(x_labels) - 0.5)
        ax1.set_ylabel(
            Ylabel_text, fontsize=font_sizes["axis_label"],
            labelpad=self._AXIS_LABEL_PAD,
        )
        ax1.set_xlabel(
            Xlabel_text, fontsize=font_sizes["axis_label"],
            labelpad=self._AXIS_LABEL_PAD,
        )

        out_stem = os.path.splitext(os.path.basename(path))[0].replace('#', '')
        pdf_dir = os.path.join('..', 'data', 'pdf')
        os.makedirs(pdf_dir, exist_ok=True)
        # Resolve layout once, then keep the same axes positions for both
        # renderers. Otherwise EPS/PNG font metrics can move the labels during
        # the tight-bounding-box export and clip text at the bottom edge.
        fig.canvas.draw()
        fig.set_layout_engine(None)
        export_bounds = fig.bbox_inches if is_categorical_bar else "tight"
        plt.savefig(os.path.join(pdf_dir, f'NWFA_{out_stem}.eps'), bbox_inches=export_bounds, pad_inches=self._EXPORT_PAD_INCHES)
        plt.savefig(os.path.join(pdf_dir, f'{out_stem}.png'),      bbox_inches=export_bounds, pad_inches=self._EXPORT_PAD_INCHES)
        plt.close(fig)
        if legend_mode == "separate":
            legend_cache = (
                self._BAR_LEGEND_HANDLES if is_categorical_bar
                else self._SMALL_SCALE_LEGEND_HANDLES if is_small_scale
                else self._SHARED_LEGEND_HANDLES
            )
            for handle, name in zip(legend_handles, legend_names):
                if is_categorical_bar and name == "UB":
                    continue
                legend_cache.setdefault(name, handle)

    @classmethod
    def export_shared_legend(cls, legend_columns=1):
        """Export separate legends for ordinary, SmallScale and bar charts."""
        cls._export_legend(cls._SHARED_LEGEND_HANDLES, "legend",
                           cls._LINE_FONT_SIZES["legend"], legend_columns)
        small_handles = {
            name: cls._SMALL_SCALE_LEGEND_HANDLES[name]
            for name in ("WPFA", "OPT")
            if name in cls._SMALL_SCALE_LEGEND_HANDLES
        }
        # Preserve the original compact eight-entry legend as a separate asset.
        compact_handles = dict(cls._SHARED_LEGEND_HANDLES)
        if "OPT" in cls._SMALL_SCALE_LEGEND_HANDLES:
            compact_handles["OPT"] = cls._SMALL_SCALE_LEGEND_HANDLES["OPT"]
        cls._export_legend(compact_handles, "legend_small",
                           cls._LINE_FONT_SIZES["legend"], 4)
        cls._export_legend(small_handles, "legend_small_scale",
                           cls._LINE_FONT_SIZES["legend"], 2)
        cls._export_legend(cls._BAR_LEGEND_HANDLES, "bar_chart_legend",
                           cls._BAR_FONT_SIZES["legend"],
                           max(1, len(cls._BAR_LEGEND_HANDLES)))

    @classmethod
    def _export_legend(cls, handles, out_stem, font_size, legend_columns):
        if not handles:
            return
        pdf_dir = os.path.join('..', 'data', 'pdf')
        os.makedirs(pdf_dir, exist_ok=True)
        legend_fig = plt.figure(figsize=(6.8, 1.0), dpi=600)
        try:
            legend = legend_fig.legend(
                list(handles.values()),
                list(handles), loc="center",
                prop={"size": font_size, "family": "Times New Roman"},
                frameon=False, handletextpad=0.5, handlelength=1,
                labelspacing=0.2, columnspacing=0.6, ncol=legend_columns,
            )
            legend_fig.canvas.draw()
            bounds = legend.get_window_extent().transformed(
                legend_fig.dpi_scale_trans.inverted()
            )
            for extension in ("png", "eps"):
                legend_fig.savefig(
                    os.path.join(pdf_dir, f"{out_stem}.{extension}"),
                    bbox_inches=bounds, pad_inches=cls._EXPORT_PAD_INCHES,
                    transparent=True,
                )
        finally:
            plt.close(legend_fig)

    @staticmethod
    def _scaled_custom_y_ticks(values, divisor, lower, upper, context):
        try:
            ticks = [float(value) / divisor for value in values]
        except (TypeError, ValueError, OverflowError) as error:
            raise ValueError(f"Invalid custom Y ticks for {context}") from error
        if not ticks or not all(math.isfinite(value) for value in ticks):
            raise ValueError(f"Custom Y ticks for {context} must be finite and nonempty")
        if any(left >= right for left, right in zip(ticks, ticks[1:])):
            raise ValueError(f"Custom Y ticks for {context} must be strictly increasing")
        tolerance = 1e-10 * max(1.0, abs(lower), abs(upper))
        if ticks[0] < lower - tolerance or ticks[-1] > upper + tolerance:
            raise ValueError(f"Custom Y ticks for {context} must stay within the Y range")
        return ticks

    @classmethod
    def _load_runtime_raw_csv(cls, path: str, sweep: str):
        required_columns = {
            "sweep", "parameter_value", "algorithm", "run_seconds",
        }
        samples = {}

        with open(path, 'r', encoding='utf-8', newline='') as csv_file:
            reader = csv.DictReader(csv_file)
            fieldnames = set(reader.fieldnames or [])
            missing_columns = required_columns - fieldnames
            if missing_columns:
                missing = ", ".join(sorted(missing_columns))
                raise ValueError(f"missing columns in {path}: {missing}")

            for line_number, row in enumerate(reader, start=2):
                if row["sweep"] != sweep:
                    continue
                algorithm = row["algorithm"]
                if algorithm not in cls._RAW_ALGO_ORDER:
                    continue
                try:
                    parameter_value = float(row["parameter_value"])
                    runtime_seconds = float(row["run_seconds"])
                except (TypeError, ValueError) as error:
                    raise ValueError(
                        f"invalid numeric value at CSV line {line_number}"
                    ) from error
                if not math.isfinite(parameter_value):
                    raise ValueError(
                        f"non-finite parameter value at CSV line {line_number}"
                    )
                if not math.isfinite(runtime_seconds) or runtime_seconds < 0:
                    raise ValueError(
                        f"invalid runtime at CSV line {line_number}"
                    )
                samples.setdefault(
                    (parameter_value, algorithm), []
                ).append(runtime_seconds)

        parameter_values = sorted({value for value, _ in samples})
        if not parameter_values:
            raise ValueError(f"no rows found for sweep {sweep!r} in {path}")

        missing_cells = [
            (value, algorithm)
            for value in parameter_values
            for algorithm in cls._RAW_ALGO_ORDER
            if not samples.get((value, algorithm))
        ]
        if missing_cells:
            preview = ", ".join(
                f"{value:g}/{algorithm}"
                for value, algorithm in missing_cells[:5]
            )
            if len(missing_cells) > 5:
                preview += ", ..."
            raise ValueError(f"incomplete runtime samples: {preview}")

        x_vals = [f"{value:g}" for value in parameter_values]
        y = [
            [
                float(np.median(samples[(value, algorithm)]))
                for value in parameter_values
            ]
            for algorithm in cls._RAW_ALGO_ORDER
        ]
        return x_vals, y

    @staticmethod
    def _gen_power_suffix(pow10: int, unit: str) -> str:
        if pow10 == 0: return ""
        return f" ($10^{{{pow10}}}$" + (f"{unit}" if unit else "") + ")"

    @staticmethod
    def _nice_step(span: float) -> float:
        if span <= 0: return 1.0
        exp = math.floor(math.log10(span))
        base = span / (10**exp)
        if base <= 1.5: m = 1
        elif base <= 3: m = 2
        elif base <= 7: m = 5
        else: m = 10
        return m * (10**exp)

    @classmethod
    def _auto_y_range(cls, ymin: float, ymax: float, target_ticks=6, padding=0.05):
        if ymin == math.inf or ymax == -math.inf: return (0.0, 1.0, 0.2)
        if ymin == ymax:
            if ymin == 0: return (0.0, 1.0, 0.2)
            pad = abs(ymin) * max(padding, 0.02)
            ymin, ymax = ymin - pad, ymax + pad
        span = ymax - ymin
        rough = span / max(2, target_ticks - 1)
        step = cls._nice_step(rough)
        start = math.floor(ymin / step) * step
        end   = math.ceil (ymax / step) * step
        return (start, end, step)

    @staticmethod
    def _pick_engineering_exp(max_abs_val: float) -> int:
        if max_abs_val <= 0: return 0
        if 1 <= max_abs_val < 1000: return 0
        exp10 = math.floor(math.log10(max_abs_val))
        eng = int(math.floor(exp10 / 3) * 3)
        scaled = max_abs_val / (10 ** eng)
        if scaled < 1 and eng - 3 >= -18: eng -= 3
        elif scaled >= 1000 and eng + 3 <= 18: eng += 3
        return eng


if __name__ == "__main__":
    _require_times_new_roman()

    Xlabels = [
        "request_cnt", "time_limit", "tao",
        "avg_memory", "mem_vary", "topo_vary", "mem_distribution",
        "path_set",
        "fidelity_threshold", "min_fidelity", "hop_count",
        "swap_prob",
    ]
    Ylabels = [
        "fidelity_gain", "succ_request_cnt", "pure_fidelity",
        "actual_req_cnt", "runtime",
    ]
    datasets = {
        "Greedy": (Xlabels, Ylabels),
        "SmallScale": (
            [
                "request_cnt", "fidelity_threshold", "tao",
                "swap_prob", "time_limit", "avg_memory",
            ],
            [
                "fidelity_gain", "succ_request_cnt",
                "actual_req_cnt", "runtime",
            ],
        ),
    }

    # Optional dataset selector, e.g.:
    #   python ChartGenerator.py SmallScale
    parser = argparse.ArgumentParser(description="Generate charts and standalone legends.")
    parser.add_argument("datasets", nargs="*", help="Greedy and/or SmallScale")
    parser.add_argument("--legend-mode", choices=("separate", "inline"), default="separate")
    parser.add_argument("--legend-columns", type=int, default=1)
    args = parser.parse_args()
    if args.legend_columns < 1:
        parser.error("--legend-columns must be positive")
    requested_datasets = {name.lower() for name in args.datasets}
    unknown_datasets = requested_datasets - {name.lower() for name in datasets}
    if unknown_datasets:
        parser.error(f"Unknown dataset: {', '.join(sorted(unknown_datasets))}. Choose from: {', '.join(datasets)}")

    OVERRIDE_LABEL_EVERY = None

    for Path, (dataset_xlabels, dataset_ylabels) in datasets.items():
        if requested_datasets and Path.lower() not in requested_datasets:
            continue
        for X in dataset_xlabels:
            for Y in dataset_ylabels:
                fname = f"{Path}_{X}_{Y}.ans"
                ChartGenerator(fname, X, Y, label_every=OVERRIDE_LABEL_EVERY,
                               legend_mode=args.legend_mode,
                               legend_columns=args.legend_columns)

    if args.legend_mode == "separate":
        ChartGenerator.export_shared_legend(args.legend_columns)
