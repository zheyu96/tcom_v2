#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import math
import sys
import numpy as np

import matplotlib
matplotlib.use("Agg")  # 無頭後端（WSL/遠端/CI）安全
import matplotlib.pyplot as plt
from matplotlib.ticker import MultipleLocator, FuncFormatter
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

    _FONT_SIZE_BASE = 30

    _RIGHT_TOP  = (0.6, 0.85)
    _LEFT_TOP   = (0.45, 0.85)
    _RIGHT_DOWN = (0.60, 0.13)
    _LEFT_DOWN  = (0.40, 0.13)

    _BBOX_POS = {
        'fidelity_gain':{
            'request_cnt': _LEFT_TOP, 'tao': _RIGHT_TOP, 'time_limit': _LEFT_TOP,
            'avg_memory': _LEFT_TOP, 'min_fidelity': _LEFT_TOP, 'fidelity_threshold': _RIGHT_TOP,
            'swap_prob': _LEFT_TOP, 'entangle_time': _RIGHT_DOWN, 'entangle_prob': _LEFT_DOWN,
            'hop_count': _RIGHT_TOP,
            'Zmin': _LEFT_TOP, 'time_eta': _LEFT_TOP, 'bucket_eps': _LEFT_TOP
        },
        'succ_request_cnt':{
            'request_cnt': _LEFT_TOP, 'tao': _RIGHT_TOP, 'time_limit': _LEFT_TOP,
            'avg_memory': _LEFT_TOP, 'min_fidelity': _LEFT_TOP, 'fidelity_threshold': _RIGHT_TOP,
            'swap_prob': _LEFT_TOP, 'entangle_time': _RIGHT_DOWN, 'entangle_prob': _LEFT_DOWN,
            'hop_count': _RIGHT_TOP,
            'Zmin': _LEFT_TOP, 'time_eta': _LEFT_TOP, 'bucket_eps': _LEFT_TOP
        },
        'pure_fidelity':{
            'request_cnt': _LEFT_TOP, 'tao': _RIGHT_TOP, 'time_limit': _LEFT_TOP,
            'avg_memory': _LEFT_TOP, 'min_fidelity': _LEFT_TOP, 'fidelity_threshold': _RIGHT_TOP,
            'swap_prob': _LEFT_TOP, 'entangle_time': _RIGHT_DOWN, 'entangle_prob': _LEFT_DOWN,
            'Zmin': _LEFT_TOP, 'time_eta': _LEFT_TOP, 'bucket_eps': _LEFT_TOP,
            'hop_count': "auto",
        },
        'actual_req_cnt':{
            'request_cnt': _LEFT_TOP, 'tao': _RIGHT_TOP, 'time_limit': _LEFT_TOP,
            'avg_memory': _LEFT_TOP, 'min_fidelity': _LEFT_TOP, 'fidelity_threshold': _RIGHT_TOP,
            'swap_prob': _LEFT_TOP, 'entangle_time': _RIGHT_DOWN, 'entangle_prob': _LEFT_DOWN,
            'hop_count': _RIGHT_TOP,
            'Zmin': _LEFT_TOP, 'time_eta': _LEFT_TOP, 'bucket_eps': _LEFT_TOP
        },
        'runtime':{
            'request_cnt': _LEFT_TOP, 'tao': _RIGHT_TOP, 'time_limit': _LEFT_TOP,
            'avg_memory': _LEFT_TOP, 'min_fidelity': _LEFT_TOP, 'fidelity_threshold': _RIGHT_TOP,
            'swap_prob': _LEFT_TOP, 'entangle_time': _RIGHT_DOWN, 'entangle_prob': _LEFT_DOWN,
            'hop_count': _RIGHT_TOP,
            'Zmin': _LEFT_TOP, 'time_eta': _LEFT_TOP, 'bucket_eps': _LEFT_TOP
        },
    }

    _Y_INTERVALS = {
        'fidelity_gain':{
            'request_cnt': (20, 120, 5, 4), 'tao': (0, 85, 5, 4),
            'time_limit': (20, 80, 5, 4), 'avg_memory': (20, 80, 5, 4),
            'min_fidelity': (23, 63, 5, 1), 'fidelity_threshold': (0, 95, 5, 6),
            'swap_prob': (10, 80, 5, 4), 'entangle_time': "auto",
            'hop_count':(10,90,5,3),
            'entangle_prob': "auto", 'Zmin': "auto", 'time_eta': "auto", 'bucket_eps': "auto"
        },
        'succ_request_cnt':{
            'request_cnt': (30, 140, 5, 6), 'tao': (0, 90, 5, 6),
            'time_limit': (40, 90, 5, 4), 'avg_memory': (30, 90, 5, 6),
            'min_fidelity': (65, 95, 5, 1), 'fidelity_threshold': (0, 105, 5, 5),
            'swap_prob': (20, 90, 5, 4), 'hop_count':(20,105,5,7),
            'entangle_time': "auto",
            'entangle_prob': "auto", 'Zmin': "auto", 'time_eta': "auto", 'bucket_eps': "auto"
        },
        'pure_fidelity':{
            'request_cnt': (50, 80, 5, 2), 'tao': (50, 70, 1, 5),
            'time_limit': (45, 70, 1, 5), 'avg_memory': (20, 40, 1, 5),
            'min_fidelity': (50, 80, 1, 5), 'fidelity_threshold': (50, 70, 1, 5),
            'swap_prob': "auto", 'entangle_time': "auto", 'entangle_prob': "auto",
            'hop_count': "auto",
            'Zmin': "auto", 'time_eta': "auto", 'bucket_eps': "auto"
        },
        'actual_req_cnt':{
            'request_cnt': (40,165,5,8), 'tao': (0,120,5,4),
            'time_limit': (50,110,5,5), 'avg_memory': (35,115,5,7),
            'min_fidelity': "auto", 'fidelity_threshold': (0,125,5,5),
            'swap_prob': (50,100,5,5), 'hop_count': "auto",
            'entangle_time': "auto",
            'entangle_prob': "auto", 'Zmin': "auto", 'time_eta': "auto", 'bucket_eps': "auto"
        },
        'runtime':{
            'request_cnt': (0,40,5,2), 'tao': "auto", 'time_limit': "auto",
            'avg_memory': "auto", 'min_fidelity': "auto", 'fidelity_threshold': "auto",
            'swap_prob': "auto", 'hop_count': "auto", 'entangle_time': "auto",
            'entangle_prob': "auto", 'Zmin': "auto", 'time_eta': "auto", 'bucket_eps': "auto"
        },
    }

    _AXIS_NAME = {
        "request_cnt": r"$\#$Requests", "time_limit": "$| T |$",
        "avg_memory": "Average Memory Limit", "tao": r"$\it{\delta}$",
        "swap_prob": "Swapping Probability", "fidelity_gain": "Exp. Werner Param. Sum",
        "succ_request_cnt": r"$\#$Exp. Accepted Requests", "pure_fidelity": "Average Final Fidelity",
        "actual_req_cnt": r"$\#$Accpected Requests",
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

    # 保持資料欄位順序，EFiRAP-long 是 main.cpp 輸出的最後一個演算法。
    _DRAW_ORDER = [0, 1, 2, 3, 4, 5, 6]

    # 保持原本各欄位使用的 marker 與顏色。
    _MARKERS = ['s', '*', 's', 'v', 'o', '^', 'x', '1', 'D']
    _COLORS = ["#800080", "#FF0000", "#0000FF", "#01C501", "#800080", "#FF00FF", "#00FFFF", "#808080", "#555555"]

    _SKIP_ALGOS = ["Nesting", "Linear", "ASAP"]

    def __init__(self, dataName: str, x_key: str, y_key: str, label_every: int|None = None):
        path = os.path.join('..', 'data', 'ans', dataName)
        if not os.path.exists(path):
            print(f"[WARN] file doesn't exist: {os.path.abspath(path)}")
            return

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

        def should_draw_algorithm(i: int) -> bool:
            name = self._ALGO_NAMES[i]
            if name in self._SKIP_ALGOS:
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

        # Y 軸處理
        raw_max = max(max(y[i]) for i in visible_indices)
        Ypow = self._pick_engineering_exp(abs(raw_max))
        Ydiv = float(10 ** Ypow) if Ypow != 0 else 1.0

        max_data, min_data = -math.inf, math.inf
        for i in range(num_algos):
            for j in range(num_rows):
                y[i][j] = y[i][j] / Ydiv
                if i in visible_indices:
                    max_data = max(max_data, y[i][j])
                    min_data = min(min_data, y[i][j])

        # 畫圖
        fig, ax1 = plt.subplots(figsize=(6.8, 5.4), dpi=600, constrained_layout=True)
        ax1.tick_params(direction="in", bottom=True, top=True, left=True, right=True, pad=10)

        plotted = []
        for draw_idx in range(min(10, num_algos)):
            if draw_idx >= len(self._DRAW_ORDER):
                break
            i = self._DRAW_ORDER[draw_idx]
            if i >= num_algos:
                continue

            # [修改] 若在跳過列表中，無論何種 X 軸都不畫
            if self._ALGO_NAMES[i] in self._SKIP_ALGOS:
                continue

            if x_key in self._ONLY_WERNER_X and self._ALGO_NAMES[i] != "G-Werner":
                continue
            
            # 針對 succ_request_cnt / actual_req_cnt，隱藏所有結尾是 UB 的項目
            if y_key in ("succ_request_cnt", "actual_req_cnt", "runtime") and self._ALGO_NAMES[i].endswith("UB"):
                continue

            ax1.plot(
                range(len(x_labels)), y[i],
                color=self._COLORS[i], lw=1, ls="-",
                marker=self._MARKERS[i], markersize=16,
                markerfacecolor='None', markeredgewidth=2.5, zorder=-draw_idx
            )
            plotted.append(i)

        legend_names = []
        for draw_idx in range(min(10, num_algos)):
            if draw_idx >= len(self._DRAW_ORDER): break
            i = self._DRAW_ORDER[draw_idx]
            if i >= num_algos: continue

            # [修改] Legend 也要過濾掉
            if self._ALGO_NAMES[i] in self._SKIP_ALGOS:
                continue

            if x_key in self._ONLY_WERNER_X and self._ALGO_NAMES[i] != "G-Werner":
                continue
            
            if y_key in ("succ_request_cnt", "actual_req_cnt", "runtime") and self._ALGO_NAMES[i].endswith("UB"):
                continue

            legend_names.append(self._ALGO_NAMES[i])

        fontsize = self._FONT_SIZE_BASE
        bbox_pos = self._BBOX_POS[y_key].get(x_key, (0.5, 0.5)) 
        
        plt.legend(
            legend_names, loc='center', bbox_to_anchor=bbox_pos,
            prop={"size": fontsize-10, "family": "Times New Roman"},
            frameon=False, handletextpad=0.5, handlelength=1,
            labelspacing=0.2, columnspacing=0.6, ncol=2, facecolor='None',
        )

        # 自動 Y 軸刻度
        cfg = self._Y_INTERVALS[y_key].get(x_key, "auto")
        manual_y_range = cfg != "auto"
        if cfg == "auto":
            Ystart, Yend, Yinterval = self._auto_y_range(min_data, max_data, target_ticks=7, padding=0.05)
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

        if label_every is not None:
            label_step_factor = int(max(1, label_every))

        if plotted and not manual_y_range:
            ymax_plotted = max(max(y[i]) for i in plotted)
            top_needed = (math.ceil(ymax_plotted / Yinterval) + 1) * Yinterval
            if top_needed > Yend:
                Yend = top_needed

        Xpow_disp_unit = " s" if x_key in ("tao", "entangle_time") else ""
        Xlabel_text = self._AXIS_NAME.get(x_key, x_key) + self._gen_power_suffix(Xpow, Xpow_disp_unit)
        Ylabel_text = self._AXIS_NAME.get(y_key, y_key) + self._gen_power_suffix(Ypow, "")

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
        ax1.tick_params(axis='y', which='major', labelsize=self._FONT_SIZE_BASE)
        ax1.tick_params(axis='y', which='minor', length=4, labelsize=0)

        def _fmt_y(val, pos):
            if abs(val) >= 100 or abs(val) == int(val): return f"{val:.0f}"
            elif abs(val) >= 10: return f"{val:.1f}"
            else: return f"{val:.2f}"
        ax1.yaxis.set_major_formatter(FuncFormatter(_fmt_y))

        plt.xticks(ticks=range(len(x_labels)), labels=x_labels, fontsize=self._FONT_SIZE_BASE+4)
        plt.ylabel(Ylabel_text, fontsize=self._FONT_SIZE_BASE+4)
        plt.xlabel(Xlabel_text, fontsize=self._FONT_SIZE_BASE+4, labelpad=100)
        ax1.yaxis.set_label_coords(-0.16, 0.45)
        ax1.xaxis.set_label_coords(0.45, -0.2)

        out_stem = os.path.splitext(os.path.basename(path))[0].replace('#', '')
        pdf_dir = os.path.join('..', 'data', 'pdf')
        os.makedirs(pdf_dir, exist_ok=True)
        plt.savefig(os.path.join(pdf_dir, f'NWFA_{out_stem}.eps'), bbox_inches="tight", pad_inches=0.3)
        plt.savefig(os.path.join(pdf_dir, f'{out_stem}.png'),      bbox_inches="tight", pad_inches=0.3)
        plt.close()

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
        "avg_memory", "fidelity_threshold", "min_fidelity", "hop_count",
        "swap_prob",
    ]
    Ylabels = [
        "fidelity_gain", "succ_request_cnt", "pure_fidelity",
        "actual_req_cnt", "runtime",
    ]
    PathNames = ["Greedy"]

    OVERRIDE_LABEL_EVERY = None

    for Path in PathNames:
        for X in Xlabels:
            for Y in Ylabels:
                fname = f"{Path}_{X}_{Y}.ans"
                ChartGenerator(fname, X, Y, label_every=OVERRIDE_LABEL_EVERY)
