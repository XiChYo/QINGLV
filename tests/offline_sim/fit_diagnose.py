#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
fit_diagnose.py  ——  M0.7 标定结果诊断（可选，本地 Mac 即可跑）

输入 calibrate_rpm_to_speed 落下的 `calib_fit.csv`，画两张图:
  1. 散点 (t, cy_mm) + 拟合直线 + 各帧 seq 标注
  2. 残差 (cy_mm - fit_mm) 柱状图

用法:
    python3 fit_diagnose.py path/to/calib_fit.csv [--out-dir DIR]

只依赖 matplotlib、pandas（也可以替换为 csv + numpy）。
"""

from __future__ import annotations

import argparse
import csv
import os
import sys
from typing import List, Tuple

try:
    import matplotlib
    matplotlib.use("Agg")  # 无显示终端也能跑
    import matplotlib.pyplot as plt
except ImportError:
    sys.stderr.write("[fit_diagnose] matplotlib 未安装,跳过画图\n")
    sys.exit(0)


def read_csv(path: str) -> Tuple[List[int], List[float], List[float], List[float], List[float]]:
    seqs, ts, cys, fits, resids = [], [], [], [], []
    with open(path, "r", encoding="utf-8") as f:
        rdr = csv.DictReader(f)
        for row in rdr:
            seqs.append(int(row["seq"]))
            ts.append(float(row["t_relative_s"]))
            cys.append(float(row["cy_mm"]))
            fits.append(float(row["fit_mm"]))
            resids.append(float(row["residual_mm"]))
    return seqs, ts, cys, fits, resids


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("csv", help="calib_fit.csv path")
    p.add_argument("--out-dir", default=None, help="输出目录(默认与 csv 同目录)")
    args = p.parse_args()

    if not os.path.exists(args.csv):
        sys.stderr.write(f"[fit_diagnose] 不存在: {args.csv}\n")
        return 2

    seqs, ts, cys, fits, resids = read_csv(args.csv)
    out_dir = args.out_dir or os.path.dirname(os.path.abspath(args.csv))
    os.makedirs(out_dir, exist_ok=True)

    # ---- 拟合 + 残差图 ----
    fig, axes = plt.subplots(2, 1, figsize=(9, 7), sharex=False)

    ax1 = axes[0]
    ax1.scatter(ts, cys, color="tab:blue", label="cy_mm (det)", zorder=3)
    ax1.plot(ts, fits, color="tab:orange", label="fit", zorder=2)
    for s, t, y in zip(seqs, ts, cys):
        ax1.annotate(str(s), (t, y), xytext=(3, 3),
                     textcoords="offset points", fontsize=7, color="gray")
    ax1.set_xlabel("t_relative (s)")
    ax1.set_ylabel("cy (mm)")
    ax1.set_title("M0.7 calibration: y_centroid vs t (single object pass)")
    ax1.legend(loc="best")
    ax1.grid(True, linestyle="--", alpha=0.4)

    ax2 = axes[1]
    bars = ax2.bar(seqs, resids, width=1.6,
                   color=["tab:red" if abs(r) > 5 else "tab:green" for r in resids])
    ax2.axhline(0.0, color="black", lw=0.8)
    ax2.set_xlabel("seq")
    ax2.set_ylabel("residual (mm)")
    ax2.set_title("Per-frame residual; red = |res|>5mm (edge frames?)")
    ax2.grid(True, linestyle="--", alpha=0.4)
    # bar 顶端打数值
    for s, r, b in zip(seqs, resids, bars):
        ax2.text(s, r + (0.2 if r >= 0 else -0.6), f"{r:+.1f}",
                 ha="center", fontsize=7, color="dimgray")

    fig.tight_layout()
    out_png = os.path.join(out_dir, "calib_diagnose.png")
    fig.savefig(out_png, dpi=120)
    print(f"[fit_diagnose] saved -> {out_png}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
