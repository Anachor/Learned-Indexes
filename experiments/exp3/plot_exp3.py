#!/usr/bin/env python3
"""Plots for experiment 3, from the CSVs written by exp3.

exp3 is exp1 on a PMA: after every insert, O'Rourke over the points
(key, slot), delta in slots. Each of its CSV rows is one k evaluated on one
prefix t, with the PMA's capacity after that insert.

One folder per n, n<N>/, against the prefix length t:
  query_complexity.png   each fixed delta, the best over all deltas, and exp1's
                         static best on the same prefix (the same keys packed,
                         y = rank) when an exp1 run has the same seed and
                         permutation
  segments.png           number of segments (lambda), each fixed delta and at the best
  best_delta.png         the best delta, the lambda it gives (each with exp1's,
                         when matched), and the PMA's density t / capacity

and overall/, across n, each point a statistic over that n's prefixes:
  query_complexity.png   the best query complexity: median, mean and max, and
                         the same for exp1's static best (dashed) at the n that
                         have a match
  best_delta.png         the best delta: median, mean and max
  overhead.png           the best minus exp1's static best: median, mean and max
                         (only when every n has a matching exp1 run)

Query complexity is log2(lambda) + log2(delta), delta = k/2, computed here from
k and L.
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

COLUMNS = ["t", "capacity", "k", "L", "fixed", "best"]
TYPES = {"t": "int64", "capacity": "int64", "k": "int64", "L": "int64", "fixed": "int8", "best": "int8"}

STATISTICS = ["median", "mean", "max"]
STATISTIC_COLORS = {"median": "tab:blue", "mean": "tab:green", "max": "tab:red"}
SERIES_COLOR = "#2a78d6"
STATIC_COLOR = "dimgray"


def cost(frame):
    """log2(lambda) + log2(delta), with delta = k/2."""
    return np.log2(frame["L"].to_numpy()) + np.log2(frame["k"].to_numpy() / 2)


def delta_label(k):
    return f"δ={k / 2:g}"


def save(fig, folder, name):
    fig.tight_layout()
    path = os.path.join(folder, name)
    fig.savefig(path, dpi=150)
    plt.close(fig)
    return path


def static_best(exp1_results, meta, n):
    """exp1's best per prefix t = 1..n, as (run, frame with t, cost, delta, L),
    from an exp1 run with the same seed and permutation that has this n; None
    when there is none."""
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
        return run, pd.DataFrame({"t": best["t"].to_numpy(), "cost": cost(best),
                                  "delta": best["k"].to_numpy() / 2, "L": best["L"].to_numpy()})
    return None


def plot_n(fixed, best, static, n, figures):
    folder = os.path.join(figures, f"n{n}")
    os.makedirs(folder, exist_ok=True)
    paths = []
    static_label = f"static best, sorted array (exp1 run {static[0]})" if static else None

    fig, axis = plt.subplots(figsize=(9, 5))
    for k, rows in fixed.groupby("k"):
        axis.plot(rows["t"], rows["cost"], lw=1, label=delta_label(k))
    axis.plot(best["t"], best["cost"], lw=1.5, ls="--", color="black", label="best")
    if static:
        axis.plot(static[1]["t"], static[1]["cost"], lw=1, ls=":", color=STATIC_COLOR, label=static_label)
    axis.set_title(f"Query complexity on a PMA, n = {n}")
    axis.set_xlabel("prefix length t")
    axis.set_ylabel("query complexity\nlog2(λ) + log2(δ), δ in slots")
    axis.legend(ncol=2)
    axis.grid(alpha=0.3)
    paths.append(save(fig, folder, "query_complexity.png"))

    fig, axis = plt.subplots(figsize=(9, 5))
    for k, rows in fixed.groupby("k"):
        axis.plot(rows["t"], rows["L"], lw=1, label=delta_label(k))
    axis.plot(best["t"], best["L"], lw=1.5, ls="--", color="black", label="best")
    axis.set_title(f"Number of segments on a PMA, n = {n}")
    axis.set_xlabel("prefix length t")
    axis.set_ylabel("number of segments λ")
    # As in exp1: small deltas reach far more segments than the best ever does;
    # cut at 4x the best's maximum so it can be read.
    axis.set_ylim(0, 4 * best["L"].max())
    axis.legend(ncol=2)
    axis.grid(alpha=0.3)
    paths.append(save(fig, folder, "segments.png"))

    # Stacked panels, not a second y-axis: δ and λ have unrelated scales.
    fig, (top, middle, bottom) = plt.subplots(3, 1, figsize=(9, 8), sharex=True,
                                              gridspec_kw={"height_ratios": [2, 2, 1]})
    top.plot(best["t"], best["k"] / 2, lw=1, color=SERIES_COLOR, label="PMA")
    middle.plot(best["t"], best["L"], lw=1, color=SERIES_COLOR, label="PMA")
    if static:
        top.plot(static[1]["t"], static[1]["delta"], lw=1, ls=":", color=STATIC_COLOR, label=static_label)
        middle.plot(static[1]["t"], static[1]["L"], lw=1, ls=":", color=STATIC_COLOR, label=static_label)
        top.legend(loc="upper left")
    bottom.plot(best["t"], best["t"] / best["capacity"], lw=1, color=SERIES_COLOR)
    top.set_ylabel("best δ (slots)")
    middle.set_ylabel("λ at the best δ")
    bottom.set_ylabel("PMA density\nt / capacity")
    bottom.set_ylim(0, 1)
    bottom.set_xlabel("prefix length t")
    for axis in (top, middle, bottom):
        axis.grid(alpha=0.3)
    top.set_title(f"Best δ, its λ, and the PMA's density, n = {n}")
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
    # Figures from an earlier layout of this folder would otherwise linger.
    for stale in glob.glob(os.path.join(folder, "*.png")):
        os.remove(stale)
    sizes = sorted(summary)
    paths = []

    def by_statistic(key, title, ylabel, name, compare=None):
        fig, axis = plt.subplots(figsize=(9, 5))
        for stat in STATISTICS:
            axis.plot(sizes, [summary[n][key][stat] for n in sizes], lw=1.5, marker="o", ms=4,
                      color=STATISTIC_COLORS[stat], label=stat if stat != "mean" else "average")
        # The same statistics of exp1's static best, dashed, at the n that have a match.
        matched = [n for n in sizes if compare in summary[n]] if compare else []
        for stat in STATISTICS if matched else []:
            axis.plot(matched, [summary[n][compare][stat] for n in matched], lw=1.5, ls="--",
                      marker="s", ms=4, color=STATISTIC_COLORS[stat],
                      label=f"{stat if stat != 'mean' else 'average'}, static best (exp1)")
        axis.set_title(title)
        axis.set_ylabel(ylabel)
        n_axis(axis, sizes)
        axis.legend()
        paths.append(save(fig, folder, name))

    by_statistic("cost", "Summary query complexity over prefixes, PMA, by n",
                 "query complexity at the best δ\nlog2(λ) + log2(δ)", "query_complexity.png",
                 compare="static")
    by_statistic("delta", "Summary optimal δ over prefixes, PMA, by n", "optimal δ (slots)", "best_delta.png",
                 compare="static_delta")
    if all("overhead" in summary[n] for n in sizes):
        by_statistic("overhead", "Summary overhead of the PMA over the sorted array, by n",
                     "PMA best minus static best\nquery complexity", "overhead.png")
    return paths


def plot_run(run, results, figures, exp1_results):
    runs.warn_if_partial(run, results)
    paths = runs.csv_paths(results, "exp3")
    if not paths:
        print(f"run {run}: no exp3_n*.csv in {results}/, skipped", file=sys.stderr)
        return

    meta = runs.meta_of(results)
    os.makedirs(figures, exist_ok=True)
    summary = {}
    for path in paths:
        n = runs.n_of(path, "exp3")
        frame = pd.read_csv(path, usecols=COLUMNS, dtype=TYPES)
        if frame["t"].max() != n:
            print(f"run {run}: {path} is cut short, skipped", file=sys.stderr)
            continue
        frame["cost"] = cost(frame)
        fixed = frame[frame["fixed"] == 1]
        best = frame[frame["best"] == 1].sort_values("t")
        static = static_best(exp1_results, meta, n)
        print("\n".join(plot_n(fixed, best, static, n, figures)))

        summary[n] = {"cost": best["cost"].agg(STATISTICS), "delta": (best["k"] / 2).agg(STATISTICS)}
        if static:
            summary[n]["static"] = static[1]["cost"].agg(STATISTICS)
            summary[n]["static_delta"] = static[1]["delta"].agg(STATISTICS)
            summary[n]["overhead"] = pd.Series(best["cost"].to_numpy() - static[1]["cost"].to_numpy()).agg(
                STATISTICS)

    if summary:
        print("\n".join(plot_overall(summary, figures)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    runs.add_arguments(parser, "exp3")
    parser.add_argument("--exp1-results", default="results/exp1",
                        help="exp1 runs to compare with: the one with the same seed and permutation")
    arguments = parser.parse_args()
    for run, results, figures in runs.chosen_runs(arguments, "exp3"):
        print(f"-- run {run}")
        plot_run(run, results, figures, arguments.exp1_results)


if __name__ == "__main__":
    main()
