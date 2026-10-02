"""Plots the PMA insertion workload: capacity, density and amortized insert cost
against the number of keys inserted, one line per insertion order.

A run is results/pma/<run>/ - the CSVs and meta.json ./tests/pma wrote; its
figure goes to figures/pma/<run>/workload.png. Which runs, as for the
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

    fig, axes = plt.subplots(4, 1, figsize=(8, 11), sharex=True)
    capacity, density, moves, time = axes

    for frame, color in ((data[o], colors[o]) for o in names):
        order = frame["order"].iloc[0]
        full = frame.dropna(subset=["moves"])
        # capacity steps up at each grow; the pre-grow rows make the steps and
        # the density sawtooth sharp
        capacity.step(frame["n"].to_numpy(), frame["capacity"].to_numpy(), where="post", lw=2, color=color,
                      label=order)
        density.plot(frame["n"].to_numpy(), frame["density"].to_numpy(), lw=1.5, color=color, label=order)
        moves.plot(full["n"].to_numpy(), full["moves_per_insert"].to_numpy(), lw=2, color=color, label=order)
        time.plot(full["n"].to_numpy(), full["ns_per_insert"].to_numpy(), lw=2, color=color, label=order)
        last = full.iloc[-1]
        for axis, column in ((moves, "moves_per_insert"), (time, "ns_per_insert")):
            axis.annotate(order, (last["n"], last[column]), xytext=(6, 0), textcoords="offset points",
                          va="center", fontsize=9, color=INK)

    n = np.logspace(0, np.log2(max(f["n"].max() for f in data.values())), 200, base=2)
    moves.plot(n, np.log2(np.maximum(n, 2)) ** 2, lw=1, ls="--", color=MUTED, label="log₂²n")
    moves.annotate("log₂²n", (n[-1], np.log2(n[-1]) ** 2), xytext=(6, 0), textcoords="offset points",
                   va="center", fontsize=9, color=MUTED)

    log2(capacity.set_yscale, "y")
    capacity.set_ylabel("capacity (slots)")
    density.set_ylabel("density (keys / slots)")
    density.axhline(root, lw=1, ls="--", color=MUTED)
    density.annotate(f"root bound {root:g}", (1, root), xytext=(4, 4), textcoords="offset points",
                     fontsize=9, color=MUTED)
    density.set_ylim(0, 1.02)
    moves.set_yscale("log")
    moves.set_ylabel("keys moved per insert\n(amortized)")
    time.set_yscale("log")
    time.set_ylabel("ns per insert\n(amortized)")
    log2(time.set_xscale, "x")
    time.set_xlabel("keys inserted (n)")

    for axis in axes:
        style(axis)
    capacity.legend(frameon=False, loc="upper left", title="insertion order")
    capacity.set_title(f"PMA insertion workload, run {run} (leaf {leaf:g}, root {root:g}, lazy growth)",
                       loc="left", color=INK)
    fig.tight_layout()
    out = Path(figures) / "workload.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"wrote {out}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    runs.add_arguments(parser, "pma")
    arguments = parser.parse_args()
    for run, results, figures in runs.chosen_runs(arguments, "pma", command="./tests/pma"):
        plot(run, results, figures)


if __name__ == "__main__":
    main()
