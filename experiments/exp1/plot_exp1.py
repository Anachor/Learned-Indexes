#!/usr/bin/env python3
"""Plots for experiment 1, from the CSVs written by exp1.

One folder per n, n<N>/, with three figures against the prefix length t:
  query_complexity.png   each fixed delta, with the best over all deltas
  segments.png           number of segments (lambda), each fixed delta and at the best delta
  best_delta.png         the best delta and the lambda it gives

and overall/, across n, each point a statistic over that n's prefixes:
  query_complexity.png   the query complexity at the best delta: median, mean and max
  best_delta.png         the best delta: median, mean and max

Query complexity is log2(lambda) + log2(delta), computed here from k and L
(delta = k/2) rather than read from the CSV's cost column: exp1 writes that
column the same way now, but earlier runs wrote log2(lambda) + log2(2 delta),
exactly 1 more. The best delta and its lambda are the same under either.
"""

import argparse
import glob
import json
import os
import re
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

COLUMNS = ["n", "t", "k", "L", "fixed", "best"]
TYPES = {"n": "int64", "t": "int64", "k": "int64", "L": "int64",
         "fixed": "int8", "best": "int8"}


def query_complexity(frame):
    """log2(lambda) + log2(delta), with delta = k/2."""
    return np.log2(frame["L"]) + np.log2(frame["k"] / 2)


def delta_label(k):
    delta = k / 2
    return f"δ={delta:g}"


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
    axis.set_ylabel("query complexity\nlog2(\u03bb) + log2(δ)")
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
    # Small deltas reach far more segments than the best ever does, and at full
    # scale the best line is flattened against the axis. Cut the axis at 4x the
    # best's maximum so it can be read; the lines above are clipped.
    axis.set_ylim(0, 4 * best["L"].max())
    axis.legend(ncol=2)
    axis.grid(alpha=0.3)
    paths.append(save(fig, folder, "segments.png"))

    fig, axis = plt.subplots(figsize=(9, 5))
    axis.plot(best["t"], best["k"] / 2, lw=1, color="tab:orange", label="best δ")
    axis.set_title(f"Best δ and its \u03bb, n = {n}")
    axis.set_xlabel("prefix length t")
    axis.set_ylabel("best δ", color="tab:orange")
    axis.grid(alpha=0.3)
    twin = axis.twinx()
    twin.plot(best["t"], best["L"], lw=1, color="tab:blue", label="\u03bb at best δ")
    twin.set_ylabel("\u03bb at the best δ", color="tab:blue")
    handles = axis.get_lines() + twin.get_lines()
    axis.legend(handles, [h.get_label() for h in handles], loc="upper right")
    paths.append(save(fig, folder, "best_delta.png"))
    return paths


STATISTICS = ["median", "mean", "max"]
STATISTIC_NAMES = {"median": "Median", "mean": "Average", "max": "Maximum"}
# The same colour per statistic on every overall figure.
STATISTIC_COLORS = {"median": "tab:blue", "mean": "tab:green", "max": "tab:red"}


def statistics(best):
    """One n's summary for the overall figures: each statistic over its prefixes t."""
    delta = best["k"] / 2
    return {stat: {"best_cost": best["cost"].agg(stat), "best_delta": delta.agg(stat)}
            for stat in STATISTICS}


def n_axis(axis, sizes):
    axis.set_xscale("log", base=2)
    axis.set_xticks(sizes)
    axis.set_xticklabels([f"{n:,}" for n in sizes], rotation=45)
    axis.minorticks_off()
    axis.set_xlabel("n")
    axis.grid(alpha=0.3)


def plot_overall(summary, figures):
    """Figures across n, from each n's statistics over its prefixes: the best
    query complexity and the best delta, each with its median, mean and max."""
    folder = os.path.join(figures, "overall")
    os.makedirs(folder, exist_ok=True)
    # Figures from an earlier layout of this folder would otherwise linger on
    # the results page.
    for stale in glob.glob(os.path.join(folder, "*.png")):
        os.remove(stale)
    sizes = sorted(summary)
    paths = []

    def by_statistic(key, title, ylabel, name):
        fig, axis = plt.subplots(figsize=(9, 5))
        for stat in STATISTICS:
            axis.plot(sizes, [summary[n][stat][key] for n in sizes],
                      lw=1.5, marker="o", ms=4, color=STATISTIC_COLORS[stat],
                      label=STATISTIC_NAMES[stat].lower())
        axis.set_title(title)
        axis.set_ylabel(ylabel)
        n_axis(axis, sizes)
        axis.legend()
        paths.append(save(fig, folder, name))

    by_statistic("best_cost", "Summary query complexity over prefixes, by n",
                 "query complexity at the best δ\nlog2(λ) + log2(δ)",
                 "query_complexity.png")
    by_statistic("best_delta", "Summary optimal δ over prefixes, by n",
                 "optimal δ", "best_delta.png")
    return paths


def all_runs(results):
    """The run numbers under results/, ascending."""
    try:
        names = os.listdir(results)
    except OSError:
        return []
    return sorted(int(d) for d in names if d.isdigit() and os.path.isdir(os.path.join(results, d)))


def csv_paths(results):
    """The run's exp1_n<N>.csv files, by n."""
    return sorted(glob.glob(os.path.join(results, "exp1_n*.csv")),
                  key=lambda p: int(re.search(r"exp1_n(\d+)\.csv$", p).group(1)))


def status_of(results):
    """The run's meta.json status, or None when it has none."""
    try:
        with open(os.path.join(results, "meta.json")) as handle:
            return json.load(handle).get("status")
    except (OSError, ValueError):
        return None


def needs_plot(results, figures):
    """True when the run has no figures, or a CSV newer than its oldest figure.

    Only the data counts: a change to this script is not seen, so after editing
    it, plot with --all.
    """
    pngs = glob.glob(os.path.join(figures, "**", "*.png"), recursive=True)
    if not pngs:
        return True
    oldest_figure = min(os.path.getmtime(p) for p in pngs)
    return any(os.path.getmtime(p) > oldest_figure for p in csv_paths(results))


def plot_run(run, results, figures):
    # A run that was stopped part-way keeps status "running": its last CSV may be
    # cut short, and plotting it would show a curve that just stops.
    status = status_of(results)
    if status is not None and status != "complete":
        print(f"warning: run {run} is {status!r}, not complete - its results may be partial",
              file=sys.stderr)

    paths = csv_paths(results)
    if not paths:
        print(f"run {run}: no exp1_n*.csv in {results}/, skipped", file=sys.stderr)
        return

    os.makedirs(figures, exist_ok=True)
    summary = {}
    for path in paths:
        n = int(re.search(r"exp1_n(\d+)\.csv$", path).group(1))
        frame = pd.read_csv(path, usecols=COLUMNS, dtype=TYPES)
        frame["cost"] = query_complexity(frame)

        best = frame[frame["best"] == 1].sort_values("t")
        fixed = frame[frame["fixed"] == 1]
        print("\n".join(plot_n(fixed, best, n, figures)))
        summary[n] = statistics(best)

    print("\n".join(plot_overall(summary, figures)))


def run_list(text):
    """--only's value: "2,3" (or "(2,3)") -> [2, 3]."""
    try:
        runs = [int(part) for part in text.strip("()[] ").split(",") if part.strip()]
    except ValueError:
        raise argparse.ArgumentTypeError(f"expected run numbers like 2,3, not {text!r}")
    if not runs:
        raise argparse.ArgumentTypeError("expected at least one run number")
    return runs


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--results", default="results/exp1", help="directory holding the runs")
    parser.add_argument("--figures", default="figures/exp1", help="directory for the per-run figure folders")
    which = parser.add_mutually_exclusive_group()
    which.add_argument("--new", action="store_true",
                       help="(default) runs with no figures, or with CSVs newer than their figures; "
                            "skips runs that have not finished")
    which.add_argument("--latest", action="store_true", help="the highest-numbered run, plotted or not")
    which.add_argument("--all", action="store_true", help="every run - after changing this script")
    which.add_argument("--only", type=run_list, metavar="RUNS", help="just these runs, e.g. --only=2,3")
    arguments = parser.parse_args()

    runs = all_runs(arguments.results)
    if not runs:
        raise SystemExit(f"no runs in {arguments.results}/ - run experiments/exp1/exp1 first")

    def places(run):
        return os.path.join(arguments.results, str(run)), os.path.join(arguments.figures, str(run))

    if arguments.only:
        missing = [run for run in arguments.only if run not in runs]
        if missing:
            raise SystemExit(f"no run {', '.join(map(str, missing))} in {arguments.results}/ "
                             f"(runs: {', '.join(map(str, runs))})")
        chosen = arguments.only
    elif arguments.latest:
        chosen = [runs[-1]]
    elif arguments.all:
        chosen = runs
    else:
        chosen = []
        for run in runs:
            results, figures = places(run)
            status = status_of(results)
            if status is not None and status != "complete":
                if needs_plot(results, figures):
                    print(f"run {run} is {status!r}, not complete - skipped; plot it with --only={run}",
                          file=sys.stderr)
                continue
            if needs_plot(results, figures):
                chosen.append(run)
        if not chosen:
            print("nothing new to plot - every finished run's figures are up to date "
                  "(--all replots everything, --only=N one run)")
            return

    for run in chosen:
        print(f"-- run {run}")
        plot_run(run, *places(run))


if __name__ == "__main__":
    main()
