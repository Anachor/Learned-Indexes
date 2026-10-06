"""Plots the PMA insertion workload: capacity, density and amortized insert cost
against the number of keys inserted, one line per insertion order.

A run is results/pma/<run>/ - the CSVs and meta.json ./tests/pma wrote; its
figures go to figures/pma/<run>/overall/: capacity.png, density.png, moves.png
and time.png, the order the results server shows them in. Which runs, as for the
experiments: --new (default) the runs with no figure or with CSVs newer than
it, skipping unfinished ones; --latest; --all; --only=2,3.

    python3 tests/plot_pma.py [--new | --latest | --all | --only=RUNS] [--results DIR] [--figures DIR]
"""

import argparse
import os
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "experiments", "common"))
import runs  # noqa: E402

# categorical slots in order; the default orders keep theirs whatever else is
# plotted, the rest take the next free ones by name
COLORS = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#4a3aa7", "#e34948"]
KNOWN = ["uniform", "zipf:16,1", "sorted", "reverse"]
INK, MUTED, GRID = "#1f1f1e", "#6b6a63", "#e4e3dc"


def log2(set_scale, axis):
    """A log2 scale under any matplotlib: base= from 3.3 (the venv's 3.7), basex=
    and basey= before (the system 3.1)."""
    try:
        set_scale("log", base=2)
    except (TypeError, ValueError):
        set_scale("log", **{"base" + axis: 2})


def style(axis):
    axis.grid(True, color=GRID, lw=0.8)
    axis.set_axisbelow(True)
    for side in ("top", "right"):
        axis.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        axis.spines[side].set_color(MUTED)
    axis.tick_params(colors=MUTED, labelcolor=INK)


def plot(run, results, figures):
    runs.warn_if_partial(run, results)
    data = {}
    for path in sorted(Path(results).glob("*.csv")):
        frame = pd.read_csv(path)
        if not frame.empty:
            data[frame["order"].iloc[0]] = frame
    if not data:
        print(f"run {run}: no CSVs in {results} - skipped", file=sys.stderr)
        return
    others = sorted(o for o in data if o not in KNOWN)
    if len(others) > len(COLORS) - len(KNOWN):
        print(f"run {run}: at most {len(COLORS) - len(KNOWN)} orders besides {', '.join(KNOWN)} fit one plot "
              f"- skipped", file=sys.stderr)
        return
    colors = {**dict(zip(KNOWN, COLORS)), **dict(zip(others, COLORS[len(KNOWN):]))}
    names = [o for o in KNOWN if o in data] + others
    pma = runs.meta_of(results).get("pma", {})
    leaf, root = pma.get("leaf_upper", 1.0), pma.get("root_upper", 0.75)

    growth = pma.get("growth", "lazy") + " growth"
    if pma.get("initial_capacity"):
        growth += f", presized to {pma['initial_capacity']:,}"
    subtitle = f"run {run} (leaf {leaf:g}, root {root:g}, {growth})"
    top = max(f["n"].max() for f in data.values())
    out = Path(figures) / "overall"
    out.mkdir(parents=True, exist_ok=True)
    # the single figure earlier versions wrote, which the four replace
    (Path(figures) / "workload.png").unlink(missing_ok=True)

    def figure(title, ylabel, draw):
        fig, axis = plt.subplots(figsize=(8, 4.5))
        for o in names:
            draw(axis, data[o], colors[o], o)
        log2(axis.set_xscale, "x")
        axis.set_xlabel("keys inserted (n)")
        axis.set_ylabel(ylabel)
        style(axis)
        axis.set_title(f"PMA {title}, {subtitle}", loc="left", color=INK, fontsize=11)
        return fig, axis

    def save(fig, name):
        fig.tight_layout()
        path = out / f"{name}.png"
        fig.savefig(path, dpi=150)
        plt.close(fig)
        print(f"wrote {path}")

    def label_ends(axis, column):
        for o in names:
            last = data[o].dropna(subset=["moves"]).iloc[-1]
            axis.annotate(o, (last["n"], last[column]), xytext=(6, 0), textcoords="offset points",
                          va="center", fontsize=9, color=INK)

    # capacity steps up at each grow; the pre-grow rows make the steps and the
    # density sawtooth sharp
    fig, axis = figure("capacity", "capacity (slots)",
                       lambda a, f, c, o: a.step(f["n"].to_numpy(), f["capacity"].to_numpy(), where="post",
                                                 lw=2, color=c, label=o))
    log2(axis.set_yscale, "y")
    axis.legend(frameon=False, loc="upper left", title="insertion order")
    save(fig, "capacity")

    fig, axis = figure("density", "density (keys / slots)",
                       lambda a, f, c, o: a.plot(f["n"].to_numpy(), f["density"].to_numpy(), lw=1.5, color=c,
                                                 label=o))
    axis.axhline(root, lw=1, ls="--", color=MUTED)
    axis.annotate(f"root bound {root:g}", (1, root), xytext=(4, 4), textcoords="offset points",
                  fontsize=9, color=MUTED)
    axis.set_ylim(0, 1.02)
    axis.legend(frameon=False, loc="upper left", title="insertion order")
    save(fig, "density")

    def amortized(column):
        def draw(a, f, c, o):
            full = f.dropna(subset=["moves"])
            a.plot(full["n"].to_numpy(), full[column].to_numpy(), lw=2, color=c, label=o)
        return draw

    fig, axis = figure("keys moved per insert", "keys moved per insert (amortized)",
                       amortized("moves_per_insert"))
    n = np.logspace(0, np.log2(top), 200, base=2)
    axis.plot(n, np.log2(np.maximum(n, 2)) ** 2, lw=1, ls="--", color=MUTED)
    axis.annotate("log₂²n", (n[-1], np.log2(n[-1]) ** 2), xytext=(6, 0), textcoords="offset points",
                  va="center", fontsize=9, color=MUTED)
    axis.set_yscale("log")
    label_ends(axis, "moves_per_insert")
    save(fig, "moves")

    fig, axis = figure("time per insert", "ns per insert (amortized)", amortized("ns_per_insert"))
    axis.set_yscale("log")
    label_ends(axis, "ns_per_insert")
    save(fig, "time")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    runs.add_arguments(parser, "pma")
    arguments = parser.parse_args()
    for run, results, figures in runs.chosen_runs(arguments, "pma", command="./tests/pma"):
        plot(run, results, figures)


if __name__ == "__main__":
    main()
