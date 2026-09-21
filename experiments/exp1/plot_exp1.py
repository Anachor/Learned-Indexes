#!/usr/bin/env python3
"""Plots for experiment 1, from the CSVs written by exp1.

One folder per n, n<N>/, with three figures against the prefix length t:
  query_complexity.png   each fixed delta, with the best over all deltas
  segments.png           number of segments (lambda), each fixed delta and at the best delta
  best_delta.png         the best delta and the lambda it gives
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


def save(fig, folder, name):
    fig.tight_layout()
    path = os.path.join(folder, name)
    fig.savefig(path, dpi=150)
    plt.close(fig)
    return path


def plot_n(fixed, best, n, figures):
    folder = os.path.join(figures, f"n{n}")
    os.makedirs(folder, exist_ok=True)
    paths = []

    fig, axis = plt.subplots(figsize=(9, 5))
    for k, rows in fixed.groupby("k"):
        axis.plot(rows["t"], rows["cost"], lw=1, label=delta_label(k))
    axis.plot(best["t"], best["cost"], lw=1.5, ls="--", color="black", label="best")
    axis.set_title(f"Query complexity, n = {n}")
    axis.set_xlabel("prefix length t")
    axis.set_ylabel("query complexity\nlog2(\u03bb) + log2(2 delta)")
    axis.legend(ncol=2)
    axis.grid(alpha=0.3)
    paths.append(save(fig, folder, "query_complexity.png"))

    fig, axis = plt.subplots(figsize=(9, 5))
    for k, rows in fixed.groupby("k"):
        axis.plot(rows["t"], rows["L"], lw=1, label=delta_label(k))
    axis.plot(best["t"], best["L"], lw=1.5, ls="--", color="black", label="best")
    axis.set_title(f"Number of segments, n = {n}")
    axis.set_xlabel("prefix length t")
    axis.set_ylabel("number of segments \u03bb")
    axis.legend(ncol=2)
    axis.grid(alpha=0.3)
    paths.append(save(fig, folder, "segments.png"))

    fig, axis = plt.subplots(figsize=(9, 5))
    axis.plot(best["t"], best["k"] / 2, lw=1, color="tab:orange", label="best delta")
    axis.set_title(f"Best delta and its \u03bb, n = {n}")
    axis.set_xlabel("prefix length t")
    axis.set_ylabel("best delta", color="tab:orange")
    axis.grid(alpha=0.3)
    twin = axis.twinx()
    twin.plot(best["t"], best["L"], lw=1, color="tab:blue", label="\u03bb at best delta")
    twin.set_ylabel("\u03bb at the best delta", color="tab:blue")
    handles = axis.get_lines() + twin.get_lines()
    axis.legend(handles, [h.get_label() for h in handles], loc="upper right")
    paths.append(save(fig, folder, "best_delta.png"))
    return paths


def latest_run(results):
    runs = [int(d) for d in os.listdir(results) if d.isdigit() and os.path.isdir(os.path.join(results, d))]
    if not runs:
        raise SystemExit(f"no runs in {results}/ - run experiments/exp1/exp1 first")
    return max(runs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", default="results/exp1", help="directory holding the runs")
    parser.add_argument("--figures", default="figures/exp1", help="directory for the per-run figure folders")
    parser.add_argument("--run", type=int, help="run number (default: the latest)")
    arguments = parser.parse_args()

    run = arguments.run if arguments.run is not None else latest_run(arguments.results)
    results = os.path.join(arguments.results, str(run))
    figures = os.path.join(arguments.figures, str(run))

    paths = sorted(glob.glob(os.path.join(results, "exp1_n*.csv")),
                   key=lambda p: int(re.search(r"exp1_n(\d+)\.csv$", p).group(1)))
    if not paths:
        raise SystemExit(f"no exp1_n*.csv in {results}/")

    os.makedirs(figures, exist_ok=True)
    for path in paths:
        n = int(re.search(r"exp1_n(\d+)\.csv$", path).group(1))
        frame = pd.read_csv(path, usecols=COLUMNS, dtype=TYPES)

        best = frame[frame["best"] == 1].sort_values("t")
        fixed = frame[frame["fixed"] == 1]
        print("\n".join(plot_n(fixed, best, n, figures)))


if __name__ == "__main__":
    main()
