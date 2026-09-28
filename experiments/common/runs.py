"""Which runs a plot script plots, shared by plot_exp1.py and plot_exp2.py.

A run is results/<exp>/<run>/ holding <exp>_n<N>.csv files and meta.json; its
figures go to figures/<exp>/<run>/. The modes:
  --new     (default) runs with no figures, or with CSVs newer than their oldest
            figure; skips runs that have not finished
  --latest  the highest-numbered run
  --all     every run - after changing the plot script, which --new cannot see
  --only    just the runs listed, e.g. --only=2,3
"""

import argparse
import glob
import json
import os
import re
import sys


def all_runs(results):
    """The run numbers under results/, ascending."""
    try:
        names = os.listdir(results)
    except OSError:
        return []
    return sorted(int(d) for d in names if d.isdigit() and os.path.isdir(os.path.join(results, d)))


def n_of(path, experiment):
    """The n of a <experiment>_n<N>.csv path."""
    return int(re.search(experiment + r"_n(\d+)\.csv$", path).group(1))


def csv_paths(results, experiment):
    """The run's <experiment>_n<N>.csv files, by n."""
    return sorted(glob.glob(os.path.join(results, experiment + "_n*.csv")), key=lambda p: n_of(p, experiment))


def meta_of(results):
    """The run's meta.json, or {} when it has none."""
    try:
        with open(os.path.join(results, "meta.json")) as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return {}


def status_of(results):
    """The run's meta.json status, or None when it has none."""
    return meta_of(results).get("status")


def needs_plot(results, figures, experiment):
    """True when the run has no figures, or a CSV newer than its oldest figure.

    Only the data counts: a change to the plot script is not seen, so after
    editing it, plot with --all.
    """
    pngs = glob.glob(os.path.join(figures, "**", "*.png"), recursive=True)
    if not pngs:
        return True
    oldest_figure = min(os.path.getmtime(p) for p in pngs)
    return any(os.path.getmtime(p) > oldest_figure for p in csv_paths(results, experiment))


def run_list(text):
    """--only's value: "2,3" (or "(2,3)") -> [2, 3]."""
    try:
        runs = [int(part) for part in text.strip("()[] ").split(",") if part.strip()]
    except ValueError:
        raise argparse.ArgumentTypeError(f"expected run numbers like 2,3, not {text!r}")
    if not runs:
        raise argparse.ArgumentTypeError("expected at least one run number")
    return runs


def add_arguments(parser, experiment):
    """--results, --figures and the run modes."""
    parser.add_argument("--results", default=f"results/{experiment}", help="directory holding the runs")
    parser.add_argument("--figures", default=f"figures/{experiment}", help="directory for the per-run figure folders")
    which = parser.add_mutually_exclusive_group()
    which.add_argument("--new", action="store_true",
                       help="(default) runs with no figures, or with CSVs newer than their figures; "
                            "skips runs that have not finished")
    which.add_argument("--latest", action="store_true", help="the highest-numbered run, plotted or not")
    which.add_argument("--all", action="store_true", help="every run - after changing this script")
    which.add_argument("--only", type=run_list, metavar="RUNS", help="just these runs, e.g. --only=2,3")


def chosen_runs(arguments, experiment):
    """The runs the arguments pick, as (run, results dir, figures dir); [] when
    there is nothing new to plot. Exits when there are no runs at all, or --only
    names a missing one."""
    runs = all_runs(arguments.results)
    if not runs:
        raise SystemExit(f"no runs in {arguments.results}/ - run experiments/{experiment}/{experiment} first")

    def places(run):
        return run, os.path.join(arguments.results, str(run)), os.path.join(arguments.figures, str(run))

    if arguments.only:
        missing = [run for run in arguments.only if run not in runs]
        if missing:
            raise SystemExit(f"no run {', '.join(map(str, missing))} in {arguments.results}/ "
                             f"(runs: {', '.join(map(str, runs))})")
        return [places(run) for run in arguments.only]
    if arguments.latest:
        return [places(runs[-1])]
    if arguments.all:
        return [places(run) for run in runs]

    chosen = []
    for run in runs:
        _, results, figures = places(run)
        status = status_of(results)
        if status is not None and status != "complete":
            if needs_plot(results, figures, experiment):
                print(f"run {run} is {status!r}, not complete - skipped; plot it with --only={run}",
                      file=sys.stderr)
            continue
        if needs_plot(results, figures, experiment):
            chosen.append(places(run))
    if not chosen:
        print("nothing new to plot - every finished run's figures are up to date "
              "(--all replots everything, --only=N one run)")
    return chosen


def warn_if_partial(run, results):
    """A run that was stopped part-way keeps status "running": its last CSV may be
    cut short, and plotting it would show a curve that just stops."""
    status = status_of(results)
    if status is not None and status != "complete":
        print(f"warning: run {run} is {status!r}, not complete - its results may be partial", file=sys.stderr)
