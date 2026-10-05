# Experiment 4

Our dynamic structure, the GPLA ([`src/GPLA/`](../../src/GPLA/README.md)): the keys are inserted into it
one at a time, once for each delta of a grid (`--deltas`, default 0.5, 1, 2, ..., 1024),
each keeping its delta throughout, and after every insert its number of segments
lambda gives the query complexity `log2(delta) + log2(lambda)`. Its PMA is [exp3](../exp3/README.md)'s, and
the seeds, `--permutation` orders and `--tiebreaker` are [exp1](../exp1/README.md)'s, so the same seed and
order give exp3's layouts: exp3's static O'Rourke on the same points is the
comparison (lambda is at most 2 * that optimum - 1, at the same delta). `--hull scan`,
`vector` or `tree` (default) only changes the time. What the experiments share:
[experiments/README.md](../README.md).

## Run

Writes one CSV per n to `results/exp4/<run>/`, a row per delta and prefix
(`seed,n,t,capacity,k,L,cost,best`, best = the grid's lowest query complexity at t),
and [`meta.json`](../README.md#runs) as exp3, with the hull and, per n and delta, the inserts' time and
work (`inserts`).

```
experiments/exp4/build.sh           # C++20
./experiments/exp4/exp4 [-n N,N,...] [-j THREADS] [--out DIR] [--deltas D,D,...] [--hull H] [--permutation P] [--tiebreaker T] [seed]
```

## Plot

Per n, `query_complexity.png` (each delta, the grid's best, exp3's static
best over the grid and over every delta, and exp1's static best), `segments.png`,
`optimality.png` (lambda over exp3's optimum at the same delta) and `best_delta.png`;
across n, `overall/query_complexity.png`, `delta_qc.png` (each delta's average),
`optimality.png`, `best_delta.png`, `overhead.png` (over exp3: what being dynamic
costs), `overhead_exp1.png` (over the sorted array, with the PMA's part) and
`insert_time.png`. The comparisons come from the runs in `--exp3-results` (same seed,
permutation and PMA) and `--exp1-results` (same seed and permutation), the same
tiebreaker preferred. Same [plot modes](../README.md#plot-modes) as exp1.

```
python3 experiments/exp4/plot_exp4.py [--new | --latest | --all | --only=RUNS] [--exp3-results DIR] [--exp1-results DIR]
```

## Validate

Small n, writes nothing: at every prefix and delta, both hulls' indexes
pass their checks and agree, hold exp3's layout, have at most 2 * optimal - 1
segments by exp3's O'Rourke, and answer `lower_bound` right for every key.

```
./experiments/exp4/exp4 --validate -n 512 [seed]
```
