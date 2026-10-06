#!/usr/bin/env python3
"""Plots for experiment 4, from the CSVs written by exp4.

exp4 inserts the keys into the GPLA (src/GPLA), once for each delta of a
grid, and records its number of segments lambda after every insert. Each CSV
row is one delta at one prefix t, with the PMA's capacity after that insert.

Two comparisons, each from the run with the same seed and permutation, and
preferably the same tiebreaker - the best delta depends on it, the query
complexity does not:
  exp3 (--exp3-results)  static O'Rourke on the same PMA layouts (its PMA
                         parameters must match too). It evaluated every
                         power-of-two k up to the span of the slots at each
                         prefix; a larger power of two has one segment, as a
                         horizontal line fits.
  exp1 (--exp1-results)  static O'Rourke on the same keys packed, y = rank: the
                         sorted array.

One folder per n, n<N>/, against the prefix length t:
  query_complexity.png   each delta, the best delta of the grid, exp3's static
                         best over the same grid and over every delta, and
                         exp1's static best
  segments.png           lambda for each delta
  optimality.png         lambda over exp3's fewest segments at the same delta:
                         at most 2 - 1/optimum
  best_delta.png         the grid's best delta and the lambda it gives, each
                         with exp3's and exp1's static best, and the PMA's
                         density

and overall/, across n, each point a statistic over that n's prefixes:
  query_complexity.png   the grid's best query complexity: median, mean and
                         max; dashed exp3's static best, dotted exp1's
  delta_qc.png           each delta's mean query complexity, and the best's:
                         which delta wins at which n
  optimality.png         each delta's mean and max of lambda over the optimum
  best_delta.png         the grid's best delta: median, mean and max; dashed
                         exp3's static best, dotted exp1's
  overhead.png           the grid's best minus exp3's static best over the same
                         grid - what being dynamic costs - and, dashed, over
                         every delta (only when every n has an exp3 match)
  overhead_exp1.png      the grid's best minus exp1's static best - the PMA and
                         being dynamic together - and, dashed, exp3's static
                         best minus exp1's, the PMA's part (only when every n
                         has an exp1 match)
  insert_time.png        microseconds per insert for each delta (meta.json)

Each comparison is drawn at the n that have a match. Query complexity is
log2(lambda) + log2(delta), delta = k/2, computed here from k and L.
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

COLUMNS = ["t", "capacity", "k", "L", "best"]  # exp4's and exp3's
TYPES = {"t": "int64", "capacity": "int64", "k": "int64", "L": "int64", "best": "int8"}
EXP1_COLUMNS = ["t", "k", "L", "best"]
EXP1_TYPES = {"t": "int64", "k": "int64", "L": "int64", "best": "int8"}

STATISTICS = ["median", "mean", "max"]
STATISTIC_COLORS = {"median": "tab:blue", "mean": "tab:green", "max": "tab:red"}
SERIES_COLOR = "#2a78d6"
EXP3_COLOR = "dimgray"  # the same PMA, static
EXP1_COLOR = "crimson"  # the sorted array, static


def cost(frame):
    """log2(lambda) + log2(delta), with delta = k/2."""
    return np.log2(frame["L"].to_numpy()) + np.log2(frame["k"].to_numpy() / 2)


def delta_label(k):
    return f"δ={k / 2:g}"


def delta_colors(ks):
    """One color per delta, dark to light as delta grows, so the order reads."""
    return dict(zip(ks, plt.cm.viridis(np.linspace(0, 0.9, len(ks)))))


def statistic_name(stat):
    return "average" if stat == "mean" else stat


def save(fig, folder, name):
    fig.tight_layout()
    path = os.path.join(folder, name)
    fig.savefig(path, dpi=150)
    plt.close(fig)
    return path


# -- the comparisons -------------------------------------------------------------

def find_run(results, experiment, meta, n, columns, types, same_pma):
    """The run of experiment (exp1 or exp3) in results/ to compare with at n: the
    same seed and permutation, the same PMA parameters when same_pma, and a
    complete <experiment>_n<N>.csv; among those, the same tiebreaker first, then
    the newest. (run, its rows) or None."""
    if not meta:
        return None
    candidates = []
    for run in runs.all_runs(results):
        other = runs.meta_of(os.path.join(results, str(run)))
        if other.get("seed") != meta.get("seed"):
            continue
        if other.get("permutation", "uniform") != meta.get("permutation", "uniform"):
            continue
        if same_pma and other.get("pma") != meta.get("pma"):
            continue
        # Runs from before --tiebreaker broke ties the mindelta way.
        candidates.append((other.get("tiebreaker", "mindelta") == meta.get("tiebreaker"), run))
    for _, run in sorted(candidates, reverse=True):
        path = os.path.join(results, str(run), f"{experiment}_n{n}.csv")
        try:
            frame = pd.read_csv(path, usecols=columns, dtype=types)
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


def static_best(rows):
    """An exp1 or exp3 run's best per prefix: t, cost, delta, L."""
    best = rows[rows["best"] == 1].sort_values("t")
    return pd.DataFrame({"t": best["t"].to_numpy(), "cost": cost(best),
                         "delta": best["k"].to_numpy() / 2, "L": best["L"].to_numpy()})


def static_grid_best(frame):
    """Per prefix, by t, the lowest query complexity of exp3's fewest segments
    over this run's grid of deltas: the static best with the same choices."""
    values = np.log2(frame["optimum"].to_numpy()) + np.log2(frame["k"].to_numpy() / 2)
    return pd.Series(values, index=frame["t"].to_numpy()).groupby(level=0).min()


# -- per n -----------------------------------------------------------------------

def plot_n(frame, exp3, exp1, n, figures):
    """frame: the run's rows with cost, and optimum when exp3 matched; exp3 and
    exp1: (run, its best per prefix) or None."""
    folder = os.path.join(figures, f"n{n}")
    os.makedirs(folder, exist_ok=True)
    paths = []
    ks = sorted(frame["k"].unique())
    colors = delta_colors(ks)
    best = frame[frame["best"] == 1].sort_values("t")
    exp3_label = f"static best, same PMA (exp3 run {exp3[0]})" if exp3 else None
    exp1_label = f"static best, sorted array (exp1 run {exp1[0]})" if exp1 else None

    fig, axis = plt.subplots(figsize=(9, 5))
    for k, rows in frame.groupby("k"):
        axis.plot(rows["t"], rows["cost"], lw=1, color=colors[k], label=delta_label(k))
    axis.plot(best["t"], best["cost"], lw=1.5, ls="--", color="black", label="best δ")
    if exp3:
        grid = static_grid_best(frame)
        axis.plot(grid.index, grid.to_numpy(), lw=1, ls="-.", color=EXP3_COLOR,
                  label=f"static best of the grid, same PMA (exp3 run {exp3[0]})")
        axis.plot(exp3[1]["t"], exp3[1]["cost"], lw=1, ls=":", color=EXP3_COLOR, label=exp3_label)
    if exp1:
        axis.plot(exp1[1]["t"], exp1[1]["cost"], lw=1, ls=":", color=EXP1_COLOR, label=exp1_label)
    axis.set_title(f"Query complexity of the GPLA, n = {n}")
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
    axis.set_title(f"Number of segments of the GPLA, n = {n}")
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
        axis.set_ylabel(f"λ / optimum\n(optimum: exp3 run {exp3[0]})")
        axis.legend(ncol=3, fontsize="small")
        axis.grid(alpha=0.3)
        paths.append(save(fig, folder, "optimality.png"))

    # Stacked panels, not a second y-axis: δ and λ have unrelated scales.
    fig, (top, middle, bottom) = plt.subplots(3, 1, figsize=(9, 8), sharex=True,
                                              gridspec_kw={"height_ratios": [2, 2, 1]})
    top.step(best["t"], best["k"] / 2, where="post", lw=1, color=SERIES_COLOR, label="GPLA, best of the grid")
    middle.plot(best["t"], best["L"], lw=1, color=SERIES_COLOR, label="GPLA, best of the grid")
    for match, color, label in ((exp3, EXP3_COLOR, exp3_label), (exp1, EXP1_COLOR, exp1_label)):
        if match:
            top.plot(match[1]["t"], match[1]["delta"], lw=1, ls=":", color=color, label=label)
            middle.plot(match[1]["t"], match[1]["L"], lw=1, ls=":", color=color, label=label)
    top.legend(loc="upper left", fontsize="small")
    top.set_yscale("log", base=2)
    bottom.plot(best["t"], best["t"] / best["capacity"], lw=1, color=SERIES_COLOR)
    top.set_ylabel("best δ\n(slots; exp1: ranks)")
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

def summarize(frame, exp3, exp1, meta, n):
    """One n's numbers for the overall figures."""
    best = frame[frame["best"] == 1].sort_values("t")
    ours = best["cost"].to_numpy()
    out = {"cost": best["cost"].agg(STATISTICS), "delta": (best["k"] / 2).agg(STATISTICS),
           "delta_cost": frame.groupby("k")["cost"].mean()}
    if exp3:
        out["exp3"] = exp3[1]["cost"].agg(STATISTICS)
        out["exp3_delta"] = exp3[1]["delta"].agg(STATISTICS)
        out["overhead"] = pd.Series(ours - exp3[1]["cost"].to_numpy()).agg(STATISTICS)
        out["overhead_grid"] = pd.Series(ours - static_grid_best(frame).to_numpy()).agg(STATISTICS)
        ratio = (frame["L"] / frame["optimum"]).groupby(frame["k"])
        out["ratio_mean"], out["ratio_max"] = ratio.mean(), ratio.max()
    if exp1:
        out["exp1"] = exp1[1]["cost"].agg(STATISTICS)
        out["exp1_delta"] = exp1[1]["delta"].agg(STATISTICS)
        out["overhead_exp1"] = pd.Series(ours - exp1[1]["cost"].to_numpy()).agg(STATISTICS)
        if exp3:
            out["pma_overhead"] = pd.Series(exp3[1]["cost"].to_numpy() - exp1[1]["cost"].to_numpy()).agg(STATISTICS)
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

    def by_statistic(key, title, ylabel, name, compares=()):
        """The statistics of summary[n][key], solid, and of each compare
        (key, label, line style, marker) at the n that have it."""
        fig, axis = plt.subplots(figsize=(9, 5))
        for stat in STATISTICS:
            axis.plot(sizes, [summary[n][key][stat] for n in sizes], lw=1.5, marker="o", ms=4,
                      color=STATISTIC_COLORS[stat], label=statistic_name(stat))
        for other, label, style, marker in compares:
            matched = [n for n in sizes if other in summary[n]]
            for stat in STATISTICS if matched else []:
                axis.plot(matched, [summary[n][other][stat] for n in matched], lw=1.5, ls=style,
                          marker=marker, ms=4, color=STATISTIC_COLORS[stat],
                          label=f"{statistic_name(stat)}, {label}")
        axis.set_title(title)
        axis.set_ylabel(ylabel)
        n_axis(axis, sizes)
        axis.legend(fontsize="small")
        paths.append(save(fig, folder, name))

    def by_delta(key, n_values, axis, **style):
        for k in ks:
            present = [n for n in n_values if k in summary[n][key]]
            axis.plot(present, [summary[n][key][k] for n in present], lw=1.2, marker="o", ms=3,
                      color=colors[k], label=delta_label(k), **style)

    by_statistic("cost", "Summary query complexity over prefixes, GPLA, by n",
                 "query complexity at the grid's best δ\nlog2(λ) + log2(δ)", "query_complexity.png",
                 compares=[("exp3", "static best, same PMA (exp3)", "--", "s"),
                           ("exp1", "static best, sorted array (exp1)", ":", "^")])

    fig, axis = plt.subplots(figsize=(9, 5))
    by_delta("delta_cost", sizes, axis)
    axis.plot(sizes, [summary[n]["cost"]["mean"] for n in sizes], lw=1.5, ls="--", color="black",
              label="best δ at each prefix")
    axis.set_title("Average query complexity over prefixes for each δ, GPLA, by n")
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

    by_statistic("delta", "Summary best δ of the grid over prefixes, GPLA, by n",
                 "best δ (slots; exp1: ranks)", "best_delta.png",
                 compares=[("exp3_delta", "static best, same PMA (exp3)", "--", "s"),
                           ("exp1_delta", "static best, sorted array (exp1)", ":", "^")])
    if all("overhead" in summary[n] for n in sizes):
        by_statistic("overhead_grid", "Summary overhead of the GPLA over the static best on the same PMA, by n",
                     "grid's best minus exp3's static best\nover the same grid (query complexity)", "overhead.png",
                     compares=[("overhead", "over exp3's static best over every δ", "--", "s")])
    if all("overhead_exp1" in summary[n] for n in sizes):
        by_statistic("overhead_exp1", "Summary overhead of the GPLA over the sorted array, by n",
                     "grid's best minus exp1's static best\n(query complexity)", "overhead_exp1.png",
                     compares=[("pma_overhead", "the PMA's part: exp3's static best minus exp1's", "--", "s")])

    timed = [n for n in sizes if summary[n]["microseconds"]]
    if timed:
        fig, axis = plt.subplots(figsize=(9, 5))
        by_delta("microseconds", timed, axis)
        axis.set_yscale("log")
        hull = (meta or {}).get("hull", "")
        axis.set_title("Time per insert, GPLA" + (f" ({hull})" if hull else "") + ", by n")
        axis.set_ylabel("µs per insert, averaged over the n inserts")
        n_axis(axis, timed)
        axis.legend(ncol=3, fontsize="small")
        paths.append(save(fig, folder, "insert_time.png"))
    return paths


def plot_run(run, results, figures, exp3_results, exp1_results):
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

        match3 = find_run(exp3_results, "exp3", meta, n, COLUMNS, TYPES, same_pma=True)
        if match3 and not same_layouts(frame, match3[1]):
            print(f"run {run}, n={n}: exp3 run {match3[0]} has the same seed and order but other PMA "
                  f"capacities - the PMA changed in between; not compared", file=sys.stderr)
            match3 = None
        if match3:
            frame["optimum"] = optimum(frame, match3[1])
        match1 = find_run(exp1_results, "exp1", meta, n, EXP1_COLUMNS, EXP1_TYPES, same_pma=False)
        exp3 = (match3[0], static_best(match3[1])) if match3 else None
        exp1 = (match1[0], static_best(match1[1])) if match1 else None
        print(f"n={n}: exp3 run {exp3[0] if exp3 else 'none'}, exp1 run {exp1[0] if exp1 else 'none'}")

        print("\n".join(plot_n(frame, exp3, exp1, n, figures)))
        summary[n] = summarize(frame, exp3, exp1, meta, n)

    if summary:
        print("\n".join(plot_overall(summary, meta, figures)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    runs.add_arguments(parser, "exp4")
    parser.add_argument("--exp3-results", default="results/exp3",
                        help="exp3 runs to compare with: the one with the same seed, permutation and PMA")
    parser.add_argument("--exp1-results", default="results/exp1",
                        help="exp1 runs to compare with: the one with the same seed and permutation")
    arguments = parser.parse_args()
    for run, results, figures in runs.chosen_runs(arguments, "exp4"):
        print(f"-- run {run}")
        plot_run(run, results, figures, arguments.exp3_results, arguments.exp1_results)


if __name__ == "__main__":
    main()
