# Experiment 1

Query complexity, `log2(lambda) + log2(delta)` (lambda = number of segments), of a static learned index
on a sorted array: for every prefix of a random permutation of {1..n}, the best
delta (1a) and a set of fixed deltas (1b).

Run folders, `meta.json`, plot modes, `--permutation` and `--tiebreaker` are shared by every experiment:
[experiments/README.md](../README.md).

## Run

Writes one CSV per n to `results/exp1/<run>/`, with a row per delta evaluated
(`seed,n,t,k,L,cost,fixed,best`, k = 2 delta, cost = log2(delta) + log2(lambda)),
and [`meta.json`](../README.md#runs).
Runs before the current definition wrote cost as log2(2 delta) + log2(lambda), 1 more; the plot
script recomputes it from k and L, so it handles both. Run 1 predates `meta.json`; its `meta.json` was
written afterwards from its CSVs.

```
experiments/exp1/build.sh           # CXX=... to pick the compiler (default g++-11 if present)
./experiments/exp1/exp1 [-n N,N,...] [-j THREADS] [--out DIR] [--permutation P] [--tiebreaker T] [seed]
```

## Plot

Reads those CSVs, writes `query_complexity.png`, `segments.png` and `best_delta.png` for each n to
`figures/exp1/<run>/n<N>/`, and the across-n summaries to `figures/exp1/<run>/overall/`.
Which runs: [plot modes](../README.md#plot-modes).

```
python3 experiments/exp1/plot_exp1.py [--new | --latest | --all | --only=RUNS] [--results DIR] [--figures DIR]
```

## Validate

Small n, writes nothing: checks the best-delta search against
trying every delta, and the segment sizes against the brute-force O'Rourke.

```
./experiments/exp1/exp1 --validate -n 512 [seed]
```

## Simulate

One prefix, writes nothing: the prefix of length T of n, with a
run's seed, for delta = 1/2, 1, 3/2, ... until no larger delta can lower
qc = log2(delta) + log2(lambda). Prints `delta,lambda,qc`; `--json` adds the keys
and the best delta's segments, `--segments K` prints only the segments for k = K
(delta = K/2). The [results server](../../web/README.md)'s Simulate tab runs it.

```
./experiments/exp1/exp1 --simulate T [--json | --segments K] [--permutation P] [--tiebreaker T] -n N seed
```
