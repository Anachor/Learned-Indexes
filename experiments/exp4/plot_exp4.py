#!/usr/bin/env python3
"""Plots for experiment 4, from the CSVs written by exp4.

exp4 inserts the keys into the learned PMA (src/LPMA), once for each delta of a
grid, and records its number of segments lambda after every insert. Each CSV
row is one delta at one prefix t, with the PMA's capacity after that insert.

The comparison is exp3's static O'Rourke on the same layouts: an exp3 run with
the same seed, permutation and PMA parameters (--exp3-results). exp3 evaluated
every power-of-two k up to the span of the slots at each prefix; a larger power
of two has one segment, as a horizontal line fits.

One folder per n, n<N>/, against the prefix length t:
  query_complexity.png   each delta, the best delta of the grid, and when
                         matched exp3's static best over the same grid and
                         over every delta
  segments.png           lambda for each delta
  optimality.png         lambda over exp3's fewest segments at the same delta
                         (when matched): at most 2 - 1/optimum
  best_delta.png         the grid's best delta and the lambda it gives (each with
                         exp3's static best, when matched), and the PMA's density

and overall/, across n, each point a statistic over that n's prefixes:
  query_complexity.png   the grid's best query complexity: median, mean and max,
                         and exp3's static best (dashed) at the matched n
  delta_qc.png           each delta's mean query complexity, and the best's:
                         which delta wins at which n
  optimality.png         each delta's mean and max of lambda over the optimum
                         (matched n)
  best_delta.png         the grid's best delta: median, mean and max
  overhead.png           the grid's best minus exp3's static best over the same
                         grid - what being dynamic costs - and, dashed, over
                         every delta: median, mean and max (only when every n
                         has a match)
  insert_time.png        microseconds per insert for each delta (meta.json)

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

COLUMNS = ["t", "capacity", "k", "L", "best"]
TYPES = {"t": "int64", "capacity": "int64", "k": "int64", "L": "int64", "best": "int8"}

STATISTICS = ["median", "mean", "max"]
STATISTIC_COLORS = {"median": "tab:blue", "mean": "tab:green", "max": "tab:red"}
SERIES_COLOR = "#2a78d6"
STATIC_COLOR = "dimgray"


def cost(frame):
    """log2(lambda) + log2(delta), with delta = k/2."""
    return np.log2(frame["L"].to_numpy()) + np.log2(frame["k"].to_numpy() / 2)


def delta_label(k):
    return f"δ={k / 2:g}"


def delta_colors(ks):
    """One color per delta, dark to light as delta grows, so the order reads."""
    return dict(zip(ks, plt.cm.viridis(np.linspace(0, 0.9, len(ks)))))


def save(fig, folder, name):
    fig.tight_layout()
    path = os.path.join(folder, name)
    fig.savefig(path, dpi=150)
    plt.close(fig)
    return path


# -- the exp3 comparison -------------------------------------------------------

def exp3_match(exp3_results, meta, n):
    """The exp3 run whose layouts are this run's at n: the same seed, permutation
    and PMA parameters, with a complete exp3_n<N>.csv. (run, its rows) or None."""
    if not meta:
        return None
    for run in reversed(runs.all_runs(exp3_results)):
        folder = os.path.join(exp3_results, str(run))
        other = runs.meta_of(folder)
        if other.get("seed") != meta.get("seed"):
            continue
        if other.get("permutation", "uniform") != meta.get("permutation", "uniform"):
            continue
        if other.get("pma") != meta.get("pma"):
            continue
        path = os.path.join(folder, f"exp3_n{n}.csv")
        try:
            frame = pd.read_csv(path, usecols=COLUMNS, dtype=TYPES)
        except (OSError, ValueError):  # missing, or empty: the run stopped before it
            continue
        if frame["t"].max() != n:
            continue  # cut short
        return run, frame
    return None


def same_layouts(frame, exp3):
    """True when both runs had the same PMA capacity after every insert - a check
    that they really hold the same layouts."""
    ours = frame.groupby("t")["capacity"].first()
    theirs = exp3.groupby("t")["capacity"].first()
    return ours.index.equals(theirs.index) and bool((ours.to_numpy() == theirs.to_numpy()).all())


def optimum(frame, exp3):
    """exp3's fewest segments at each of the frame's rows (t, k): its row for that
    k, or 1 for a k above every k it evaluated at t; NaN when it has neither."""
    rows = exp3[["t", "k", "L"]].rename(columns={"L": "optimum"})
    merged = frame[["t", "k"]].merge(rows, on=["t", "k"], how="left")
    largest = merged["t"].map(exp3.groupby("t")["k"].max()).to_numpy()
    above = merged["optimum"].isna().to_numpy() & (merged["k"].to_numpy() > largest)
    values = merged["optimum"].to_numpy(dtype=float)
    values[above] = 1
    return values


def static_best(exp3):
    """exp3's best per prefix: t, cost, delta, L."""
    best = exp3[exp3["best"] == 1].sort_values("t")
    return pd.DataFrame({"t": best["t"].to_numpy(), "cost": cost(best),
                         "delta": best["k"].to_numpy() / 2, "L": best["L"].to_numpy()})


def static_grid_best(frame):
    """Per prefix, by t, the lowest query complexity of exp3's fewest segments
    over this run's grid of deltas: the static best with the same choices."""
    values = np.log2(frame["optimum"].to_numpy()) + np.log2(frame["k"].to_numpy() / 2)
    return pd.Series(values, index=frame["t"].to_numpy()).groupby(level=0).min()


# -- per n -----------------------------------------------------------------------

def plot_n(frame, static, n, figures):
    """frame: the run's rows with cost, and optimum when matched; static: (exp3
    run, its best per prefix) or None."""
    folder = os.path.join(figures, f"n{n}")
    os.makedirs(folder, exist_ok=True)
    paths = []
    ks = sorted(frame["k"].unique())
    colors = delta_colors(ks)
    best = frame[frame["best"] == 1].sort_values("t")
    static_label = f"static best, any δ (exp3 run {static[0]})" if static else None

    fig, axis = plt.subplots(figsize=(9, 5))
    for k, rows in frame.groupby("k"):
        axis.plot(rows["t"], rows["cost"], lw=1, color=colors[k], label=delta_label(k))
    axis.plot(best["t"], best["cost"], lw=1.5, ls="--", color="black", label="best δ")
    if static:
        grid = static_grid_best(frame)
        axis.plot(grid.index, grid.to_numpy(), lw=1, ls="-.", color=STATIC_COLOR,
                  label=f"static best of the grid (exp3 run {static[0]})")
        axis.plot(static[1]["t"], static[1]["cost"], lw=1, ls=":", color=STATIC_COLOR, label=static_label)
    axis.set_title(f"Query complexity of the learned PMA, n = {n}")
    axis.set_xlabel("prefix length t")
    axis.set_ylabel("query complexity\nlog2(λ) + log2(δ), δ in slots")
    axis.legend(ncol=3, fontsize="small")
    axis.grid(alpha=0.3)
    paths.append(save(fig, folder, "query_complexity.png"))

    fig, axis = plt.subplots(figsize=(9, 5))
    for k, rows in frame.groupby("k"):
        axis.plot(rows["t"], rows["L"], lw=1, color=colors[k], label=delta_label(k))
    axis.plot(best["t"], best["L"], lw=1.5, ls="--", color="black", label="best δ")
    axis.set_yscale("log")
    axis.set_title(f"Number of segments of the learned PMA, n = {n}")
    axis.set_xlabel("prefix length t")
    axis.set_ylabel("number of segments λ")
    axis.legend(ncol=3, fontsize="small")
    axis.grid(alpha=0.3)
    paths.append(save(fig, folder, "segments.png"))

    if "optimum" in frame:
        fig, axis = plt.subplots(figsize=(9, 5))
        for k, rows in frame.groupby("k"):
            axis.plot(rows["t"], rows["L"] / rows["optimum"], lw=1, color=colors[k], label=delta_label(k))
        axis.axhline(2, ls=":", color="black", label="bound: 2 - 1/optimum")
        axis.set_ylim(0.95, 2.05)
        axis.set_title(f"Segments over the fewest possible, same layout and δ, n = {n}")
        axis.set_xlabel("prefix length t")
        axis.set_ylabel(f"λ / optimum\n(optimum: exp3 run {static[0]})")
        axis.legend(ncol=3, fontsize="small")
        axis.grid(alpha=0.3)
        paths.append(save(fig, folder, "optimality.png"))

    # Stacked panels, not a second y-axis: δ and λ have unrelated scales.
    fig, (top, middle, bottom) = plt.subplots(3, 1, figsize=(9, 8), sharex=True,
                                              gridspec_kw={"height_ratios": [2, 2, 1]})
    top.step(best["t"], best["k"] / 2, where="post", lw=1, color=SERIES_COLOR, label="learned PMA, best of the grid")
    middle.plot(best["t"], best["L"], lw=1, color=SERIES_COLOR, label="learned PMA, best of the grid")
    if static:
        top.plot(static[1]["t"], static[1]["delta"], lw=1, ls=":", color=STATIC_COLOR, label=static_label)
        middle.plot(static[1]["t"], static[1]["L"], lw=1, ls=":", color=STATIC_COLOR, label=static_label)
    top.legend(loc="upper left")
    top.set_yscale("log", base=2)
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


# -- across n --------------------------------------------------------------------

def summarize(frame, static, meta, n):
    """One n's numbers for the overall figures."""
    best = frame[frame["best"] == 1].sort_values("t")
    out = {"cost": best["cost"].agg(STATISTICS), "delta": (best["k"] / 2).agg(STATISTICS),
           "delta_cost": frame.groupby("k")["cost"].mean()}
    if static:
        out["static"] = static[1]["cost"].agg(STATISTICS)
        out["static_delta"] = static[1]["delta"].agg(STATISTICS)
        out["overhead"] = pd.Series(best["cost"].to_numpy() - static[1]["cost"].to_numpy()).agg(STATISTICS)
        grid = static_grid_best(frame)
        out["overhead_grid"] = pd.Series(best["cost"].to_numpy() - grid.to_numpy()).agg(STATISTICS)
        ratio = (frame["L"] / frame["optimum"]).groupby(frame["k"])
        out["ratio_mean"], out["ratio_max"] = ratio.mean(), ratio.max()
    inserts = ((meta or {}).get("inserts") or {}).get(str(n), {})
    seconds = {k: inserts.get(f"{k / 2:g}", {}).get("seconds") for k in sorted(frame["k"].unique())}
    out["microseconds"] = {k: 1e6 * s / n for k, s in seconds.items() if s is not None}
    return out


def n_axis(axis, sizes):
    axis.set_xscale("log", base=2)
    axis.set_xticks(sizes)
    axis.set_xticklabels([f"{n:,}" for n in sizes], rotation=45)
    axis.minorticks_off()
    axis.set_xlabel("n")
    axis.grid(alpha=0.3)


def plot_overall(summary, meta, figures):
    folder = os.path.join(figures, "overall")
    os.makedirs(folder, exist_ok=True)
    # Figures from an earlier layout of this folder would otherwise linger.
    for stale in glob.glob(os.path.join(folder, "*.png")):
        os.remove(stale)
    sizes = sorted(summary)
    ks = sorted(set().union(*(summary[n]["delta_cost"].index for n in sizes)))
    colors = delta_colors(ks)
    paths = []

    def by_statistic(key, title, ylabel, name, compare=None, compare_label="static best (exp3)"):
        fig, axis = plt.subplots(figsize=(9, 5))
        for stat in STATISTICS:
            axis.plot(sizes, [summary[n][key][stat] for n in sizes], lw=1.5, marker="o", ms=4,
                      color=STATISTIC_COLORS[stat], label=stat if stat != "mean" else "average")
        # The same statistics of compare, dashed, at the n that have it.
        matched = [n for n in sizes if compare in summary[n]] if compare else []
        for stat in STATISTICS if matched else []:
            axis.plot(matched, [summary[n][compare][stat] for n in matched], lw=1.5, ls="--",
                      marker="s", ms=4, color=STATISTIC_COLORS[stat],
                      label=f"{stat if stat != 'mean' else 'average'}, {compare_label}")
        axis.set_title(title)
        axis.set_ylabel(ylabel)
        n_axis(axis, sizes)
        axis.legend()
        paths.append(save(fig, folder, name))

    def by_delta(key, n_values, axis, **style):
        for k in ks:
            present = [n for n in n_values if k in summary[n][key]]
            axis.plot(present, [summary[n][key][k] for n in present], lw=1.2, marker="o", ms=3,
                      color=colors[k], label=delta_label(k), **style)

    by_statistic("cost", "Summary query complexity over prefixes, learned PMA, by n",
                 "query complexity at the grid's best δ\nlog2(λ) + log2(δ)", "query_complexity.png",
                 compare="static")

    fig, axis = plt.subplots(figsize=(9, 5))
    by_delta("delta_cost", sizes, axis)
    axis.plot(sizes, [summary[n]["cost"]["mean"] for n in sizes], lw=1.5, ls="--", color="black",
              label="best δ at each prefix")
    axis.set_title("Average query complexity over prefixes for each δ, learned PMA, by n")
    axis.set_ylabel("average query complexity\nlog2(λ) + log2(δ)")
    n_axis(axis, sizes)
    axis.legend(ncol=3, fontsize="small")
    paths.append(save(fig, folder, "delta_qc.png"))

    matched = [n for n in sizes if "ratio_mean" in summary[n]]
    if matched:
        fig, (top, bottom) = plt.subplots(2, 1, figsize=(9, 8), sharex=True)
        by_delta("ratio_mean", matched, top)
        by_delta("ratio_max", matched, bottom)
        bottom.axhline(2, ls=":", color="black", label="bound")
        top.set_ylabel("average λ / optimum")
        bottom.set_ylabel("largest λ / optimum")
        top.set_title("Segments over the fewest possible (exp3, same layout and δ), by n")
        n_axis(top, matched)
        n_axis(bottom, matched)
        top.set_xlabel("")
        top.legend(ncol=3, fontsize="small")
        paths.append(save(fig, folder, "optimality.png"))

    by_statistic("delta", "Summary best δ of the grid over prefixes, learned PMA, by n", "best δ (slots)",
                 "best_delta.png", compare="static_delta")
    if all("overhead" in summary[n] for n in sizes):
        by_statistic("overhead_grid", "Summary overhead of the learned PMA over the static best, by n",
                     "grid's best minus exp3's static best\nover the same grid (query complexity)", "overhead.png",
                     compare="overhead", compare_label="over the static best over every δ")

    timed = [n for n in sizes if summary[n]["microseconds"]]
    if timed:
        fig, axis = plt.subplots(figsize=(9, 5))
        by_delta("microseconds", timed, axis)
        axis.set_yscale("log")
        hull = (meta or {}).get("hull", "")
        axis.set_title("Time per insert, learned PMA" + (f" ({hull})" if hull else "") + ", by n")
        axis.set_ylabel("µs per insert, averaged over the n inserts")
        n_axis(axis, timed)
        axis.legend(ncol=3, fontsize="small")
        paths.append(save(fig, folder, "insert_time.png"))
    return paths


def plot_run(run, results, figures, exp3_results):
    runs.warn_if_partial(run, results)
    paths = runs.csv_paths(results, "exp4")
    if not paths:
        print(f"run {run}: no exp4_n*.csv in {results}/, skipped", file=sys.stderr)
        return

    meta = runs.meta_of(results)
    os.makedirs(figures, exist_ok=True)
    summary = {}
    for path in paths:
        n = runs.n_of(path, "exp4")
        frame = pd.read_csv(path, usecols=COLUMNS, dtype=TYPES)
        if frame["t"].max() != n:
            print(f"run {run}: {path} is cut short, skipped", file=sys.stderr)
            continue
        frame["cost"] = cost(frame)
        match = exp3_match(exp3_results, meta, n)
        if match and not same_layouts(frame, match[1]):
            print(f"run {run}, n={n}: exp3 run {match[0]} has the same seed and order but other PMA "
                  f"capacities - the PMA changed in between; not compared", file=sys.stderr)
            match = None
        static = None
        if match:
            frame["optimum"] = optimum(frame, match[1])
            static = (match[0], static_best(match[1]))
        print("\n".join(plot_n(frame, static, n, figures)))
        summary[n] = summarize(frame, static, meta, n)

    if summary:
        print("\n".join(plot_overall(summary, meta, figures)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    runs.add_arguments(parser, "exp4")
    parser.add_argument("--exp3-results", default="results/exp3",
                        help="exp3 runs to compare with: the one with the same seed, permutation and PMA")
    arguments = parser.parse_args()
    for run, results, figures in runs.chosen_runs(arguments, "exp4"):
        print(f"-- run {run}")
        plot_run(run, results, figures, arguments.exp3_results)


if __name__ == "__main__":
    main()
