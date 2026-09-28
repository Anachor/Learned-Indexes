#!/usr/bin/env python3
"""Plots for experiment 2, from the CSVs written by exp2.

exp2 writes one row group per build: the prefix t it was built at, its level
and size, and every k it evaluated. The levels at prefix t follow from t's
bits: level i is non-empty when bit i of t is set, and was built at t with its
low i bits cleared. Summing over them gives, for every prefix t:
  2a  cost at a fixed delta: sum over levels of log2(delta) + log2(lambda_i)
  2b  cost with each level's own best delta: sum of log2(delta_i) + log2(lambda_i)

One folder per n, n<N>/, against the prefix length t:
  query_complexity.png   2a for each fixed delta, 2b, and exp1's static best on
                         the same prefix when an exp1 run has the same seed and
                         permutation
  segments.png           total lambda over the levels, each fixed delta and 2b
  best_delta.png         each build's own best delta, at the t it was built,
                         coloured by level

and overall/, across n, each point a statistic over that n's prefixes:
  query_complexity.png   2b: median, mean and max
  overhead.png           2b minus exp1's static best: median, mean and max
                         (only when every n has a matching exp1 run)

Query complexity per level is log2(lambda) + log2(delta), delta = k/2, computed
here from k and L. A level exact at delta = 1/2 with one segment costs -1, so
small levels lower the sum.
"""

import argparse
import glob
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))
import runs  # noqa: E402

COLUMNS = ["t", "level", "size", "k", "L", "fixed", "best"]
TYPES = {"t": "int64", "level": "int64", "size": "int64", "k": "int64", "L": "int64",
         "fixed": "int8", "best": "int8"}

STATISTICS = ["median", "mean", "max"]
STATISTIC_COLORS = {"median": "tab:blue", "mean": "tab:green", "max": "tab:red"}


def level_cost(k, L):
    """log2(lambda) + log2(delta), with delta = k/2."""
    return np.log2(L) + np.log2(k / 2)


def delta_label(k):
    return f"δ={k / 2:g}"


def save(fig, folder, name):
    fig.tight_layout()
    path = os.path.join(folder, name)
    fig.savefig(path, dpi=150)
    plt.close(fig)
    return path


def over_levels(n, per_build):
    """Sums a per-build quantity over the levels present at each prefix.

    per_build[b] is the value of the level built at prefix b (index 0 unused).
    Returns an array indexed by t = 1..n (index 0 unused).
    """
    t = np.arange(n + 1)
    total = np.zeros(n + 1)
    for i in range(int(n).bit_length()):
        present = (t >> i) & 1 == 1
        built = t & ~((1 << i) - 1)
        total[present] += per_build[built[present]]
    return total


def prefixes(frame, n):
    """Per prefix t = 1..n: the 2a and 2b costs and total lambdas, and the level count."""
    out = pd.DataFrame({"t": np.arange(1, n + 1)})
    out["levels"] = [bin(t).count("1") for t in range(1, n + 1)]

    fixed = frame[frame["fixed"] == 1]
    for k, rows in fixed.groupby("k"):
        per_build = np.zeros(n + 1)
        per_build[rows["t"].to_numpy()] = level_cost(rows["k"].to_numpy(), rows["L"].to_numpy())
        lam = np.zeros(n + 1)
        lam[rows["t"].to_numpy()] = rows["L"].to_numpy()
        out[f"cost_k{k}"] = over_levels(n, per_build)[1:]
        out[f"L_k{k}"] = over_levels(n, lam)[1:]

    best = frame[frame["best"] == 1]
    per_build = np.zeros(n + 1)
    per_build[best["t"].to_numpy()] = level_cost(best["k"].to_numpy(), best["L"].to_numpy())
    lam = np.zeros(n + 1)
    lam[best["t"].to_numpy()] = best["L"].to_numpy()
    out["cost_best"] = over_levels(n, per_build)[1:]
    out["L_best"] = over_levels(n, lam)[1:]
    return out


def static_best(exp1_results, meta, n):
    """exp1's best cost per prefix t = 1..n, from an exp1 run with the same seed and
    permutation that has this n; None when there is none."""
    if not meta:
        return None
    for run in reversed(runs.all_runs(exp1_results)):
        folder = os.path.join(exp1_results, str(run))
        other = runs.meta_of(folder)
        if other.get("seed") != meta.get("seed"):
            continue
        if other.get("permutation", "uniform") != meta.get("permutation", "uniform"):
            continue
        path = os.path.join(folder, f"exp1_n{n}.csv")
        if not os.path.exists(path):
            continue
        frame = pd.read_csv(path, usecols=["t", "k", "L", "best"])
        best = frame[frame["best"] == 1].sort_values("t")
        if len(best) != n:
            continue  # cut short
        return run, level_cost(best["k"].to_numpy(), best["L"].to_numpy())
    return None


def plot_n(frame, table, static, n, figures):
    folder = os.path.join(figures, f"n{n}")
    os.makedirs(folder, exist_ok=True)
    ks = sorted(frame.loc[frame["fixed"] == 1, "k"].unique())
    paths = []

    fig, axis = plt.subplots(figsize=(9, 5))
    for k in ks:
        axis.plot(table["t"], table[f"cost_k{k}"], lw=1, label=delta_label(k))
    axis.plot(table["t"], table["cost_best"], lw=1.5, ls="--", color="black", label="own δ per level")
    if static is not None:
        run, cost = static
        axis.plot(table["t"], cost, lw=1, ls=":", color="dimgray", label=f"static best (exp1 run {run})")
    axis.set_title(f"Query complexity, Bentley-Saxe, n = {n}")
    axis.set_xlabel("prefix length t")
    axis.set_ylabel("query complexity\nΣ over levels log2(λ) + log2(δ)")
    axis.legend(ncol=2)
    axis.grid(alpha=0.3)
    paths.append(save(fig, folder, "query_complexity.png"))

    fig, axis = plt.subplots(figsize=(9, 5))
    for k in ks:
        axis.plot(table["t"], table[f"L_k{k}"], lw=1, label=delta_label(k))
    axis.plot(table["t"], table["L_best"], lw=1.5, ls="--", color="black", label="own δ per level")
    axis.set_title(f"Number of segments over all levels, n = {n}")
    axis.set_xlabel("prefix length t")
    axis.set_ylabel("Σ over levels λ")
    # As in exp1: cut at 4x the own-delta line's maximum so it can be read.
    axis.set_ylim(0, 4 * table["L_best"].max())
    axis.legend(ncol=2)
    axis.grid(alpha=0.3)
    paths.append(save(fig, folder, "segments.png"))

    best = frame[frame["best"] == 1]
    fig, axis = plt.subplots(figsize=(9, 5))
    levels = sorted(best["level"].unique())
    colors = plt.cm.viridis(np.linspace(0, 1, len(levels)))
    for level, color in zip(levels, colors):
        rows = best[best["level"] == level]
        axis.scatter(rows["t"], rows["k"] / 2, s=6 if level < 4 else 14, color=color,
                     label=f"level {level} ({1 << level:,} keys)")
    axis.set_yscale("log", base=2)
    axis.set_title(f"Each level's own best δ, when it is built, n = {n}")
    axis.set_xlabel("prefix length t at the build")
    axis.set_ylabel("best δ of the level built")
    axis.legend(ncol=2, fontsize=7, markerscale=2)
    axis.grid(alpha=0.3)
    paths.append(save(fig, folder, "best_delta.png"))
    return paths


def n_axis(axis, sizes):
    axis.set_xscale("log", base=2)
    axis.set_xticks(sizes)
    axis.set_xticklabels([f"{n:,}" for n in sizes], rotation=45)
    axis.minorticks_off()
    axis.set_xlabel("n")
    axis.grid(alpha=0.3)


def plot_overall(summary, figures):
    folder = os.path.join(figures, "overall")
    os.makedirs(folder, exist_ok=True)
    for stale in glob.glob(os.path.join(folder, "*.png")):
        os.remove(stale)
    sizes = sorted(summary)
    paths = []

    def by_statistic(key, title, ylabel, name):
        fig, axis = plt.subplots(figsize=(9, 5))
        for stat in STATISTICS:
            axis.plot(sizes, [summary[n][key][stat] for n in sizes], lw=1.5, marker="o", ms=4,
                      color=STATISTIC_COLORS[stat], label=stat if stat != "mean" else "average")
        axis.set_title(title)
        axis.set_ylabel(ylabel)
        n_axis(axis, sizes)
        axis.legend()
        paths.append(save(fig, folder, name))

    by_statistic("cost_best", "Summary query complexity over prefixes, own δ per level, by n",
                 "query complexity\nΣ over levels log2(λ) + log2(δ)", "query_complexity.png")
    if all("overhead" in summary[n] for n in sizes):
        by_statistic("overhead", "Summary overhead over the static index, by n",
                     "Bentley-Saxe (own δ) minus static best\nquery complexity", "overhead.png")
    return paths


def plot_run(run, results, figures, exp1_results):
    runs.warn_if_partial(run, results)
    paths = runs.csv_paths(results, "exp2")
    if not paths:
        print(f"run {run}: no exp2_n*.csv in {results}/, skipped", file=sys.stderr)
        return

    meta = runs.meta_of(results)
    os.makedirs(figures, exist_ok=True)
    summary = {}
    for path in paths:
        n = runs.n_of(path, "exp2")
        frame = pd.read_csv(path, usecols=COLUMNS, dtype=TYPES)
        if frame["t"].max() != n:
            print(f"run {run}: {path} is cut short, skipped", file=sys.stderr)
            continue
        table = prefixes(frame, n)
        static = static_best(exp1_results, meta, n)
        print("\n".join(plot_n(frame, table, static, n, figures)))

        summary[n] = {"cost_best": table["cost_best"].agg(STATISTICS)}
        if static is not None:
            summary[n]["overhead"] = (table["cost_best"] - static[1]).agg(STATISTICS)

    if summary:
        print("\n".join(plot_overall(summary, figures)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    runs.add_arguments(parser, "exp2")
    parser.add_argument("--exp1-results", default="results/exp1",
                        help="exp1 runs to compare with: the one with the same seed and permutation")
    arguments = parser.parse_args()
    for run, results, figures in runs.chosen_runs(arguments, "exp2"):
        print(f"-- run {run}")
        plot_run(run, results, figures, arguments.exp1_results)


if __name__ == "__main__":
    main()
