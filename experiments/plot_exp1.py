#!/usr/bin/env python3
"""Plots for experiment 1, from the CSVs written by exp1.

One figure per n for each part:
  exp1a_n<N>.png   the best query complexity per prefix, and the delta reaching it
  exp1b_n<N>.png   the fixed deltas per prefix, against that best
"""

import argparse
import glob
import os
import re

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

COLUMNS = ["n", "t", "k", "L", "cost", "fixed", "best"]
TYPES = {"n": "int64", "t": "int64", "k": "int64", "L": "int64",
         "cost": "float64", "fixed": "int8", "best": "int8"}


def delta_label(k):
    delta = k / 2
    return f"delta={delta:g}"


def plot_1a(best, n, figures):
    fig, (top, bottom) = plt.subplots(2, 1, sharex=True, figsize=(9, 7),
                                      gridspec_kw={"height_ratios": [2, 1]})

    top.plot(best["t"], best["cost"], lw=1, label="best query complexity")
    top.plot(best["t"], np.log2(best["t"]), lw=1, ls="--", color="gray",
             label="log2 t (binary search)")
    top.set_ylabel("query complexity\nlog2(segment size) + log2(2 delta)")
    top.set_title(f"Experiment 1a, n = {n}")
    top.legend()
    top.grid(alpha=0.3)

    bottom.plot(best["t"], best["k"] / 2, lw=1, color="tab:orange")
    bottom.set_yscale("log", base=2)
    bottom.set_ylabel("best delta")
    bottom.set_xlabel("prefix length t")
    bottom.grid(alpha=0.3)

    bottom.set_xscale("log", base=2)
    fig.tight_layout()
    path = os.path.join(figures, f"exp1a_n{n}.png")
    fig.savefig(path, dpi=150)
    plt.close(fig)
    return path


def plot_1b(fixed, best, n, figures):
    fig, axis = plt.subplots(figsize=(9, 6))

    for k, rows in fixed.groupby("k"):
        rows = rows.sort_values("t")
        axis.plot(rows["t"], rows["cost"], lw=1, label=delta_label(k))
    axis.plot(best["t"], best["cost"], lw=1.5, ls="--", color="black", label="best (1a)")

    axis.set_xscale("log", base=2)
    axis.set_xlabel("prefix length t")
    axis.set_ylabel("query complexity\nlog2(segment size) + log2(2 delta)")
    axis.set_title(f"Experiment 1b, n = {n}")
    axis.legend(ncol=2)
    axis.grid(alpha=0.3)

    fig.tight_layout()
    path = os.path.join(figures, f"exp1b_n{n}.png")
    fig.savefig(path, dpi=150)
    plt.close(fig)
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", default="results", help="directory holding exp1_n*.csv")
    parser.add_argument("--figures", default="figures", help="directory for the PNGs")
    arguments = parser.parse_args()

    paths = sorted(glob.glob(os.path.join(arguments.results, "exp1_n*.csv")),
                   key=lambda p: int(re.search(r"exp1_n(\d+)\.csv$", p).group(1)))
    if not paths:
        raise SystemExit(f"no exp1_n*.csv in {arguments.results}/ - run experiments/exp1 first")

    os.makedirs(arguments.figures, exist_ok=True)
    for path in paths:
        n = int(re.search(r"exp1_n(\d+)\.csv$", path).group(1))
        frame = pd.read_csv(path, usecols=COLUMNS, dtype=TYPES)

        best = frame[frame["best"] == 1].sort_values("t")
        fixed = frame[frame["fixed"] == 1]
        print(plot_1a(best, n, arguments.figures))
        print(plot_1b(fixed, best, n, arguments.figures))


if __name__ == "__main__":
    main()
