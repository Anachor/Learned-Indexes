# Experiment 2

Query complexity of a dynamic learned index: Bentley-Saxe over static O'Rourke
indexes, as in the PGM paper - base 2, no buffer. Levels 0, 1, 2, ... are each
empty or hold exactly 2^i keys; inserting the t-th key merges levels 0..i-1 and the
key into the first empty level i. After t inserts the non-empty levels are the
1-bits of t. Each level is its own static index (ranks within the level), segmented
when it is built:

- 2a: a fixed delta on every level (0.5, 1, 2, ..., 32), cost = sum over levels of
  `log2(delta) + log2(lambda_i)`;
- 2b: each level with its own best delta (exp1's search), cost = sum of `log2(delta_i) + log2(lambda_i)`.

Same seeds (n uses seed + n), `--permutation` orders and `--tiebreaker` (for each level's own delta) as
[exp1](../exp1/README.md), so the same seed and order give exp1's prefixes, and exp1's static best is the
comparison. What the experiments share: [experiments/README.md](../README.md).

## Run

Writes one CSV per n to `results/exp2/<run>/`, one row group per build -
the prefix it was built at and the level it built, with every k evaluated on it
(`seed,n,t,level,size,k,L,cost,fixed,best`, cost = that level's `log2(delta) + log2(lambda)`).
The levels at prefix t are t's 1-bits, level i built at t with its low i bits
cleared, so the plot script sums them. Also writes [`meta.json`](../README.md#runs), as exp1.

```
experiments/exp2/build.sh
./experiments/exp2/exp2 [-n N,N,...] [--out DIR] [--permutation P] [--tiebreaker T] [seed]
```

## Plot

Per n, `query_complexity.png` (2a per delta, 2b, and exp1's static best
when an exp1 run in `--exp1-results` has the same seed and permutation),
`segments.png` (total lambda over levels) and `best_delta.png` (each build's own
delta, one panel per level) and `average_best_delta.png` (its average per level, with exp1's best delta at
t = 2^level dotted); across n, `overall/query_complexity.png` (2b, with
exp1's static best dashed where it matches) and
`overall/overhead.png` (2b minus static best, when every n has a match).
Same [plot modes](../README.md#plot-modes) as exp1.

```
python3 experiments/exp2/plot_exp2.py [--new | --latest | --all | --only=RUNS] [--exp1-results DIR]
```

## Validate

Small n, writes nothing: checks the levels (sizes are t's bits,
each holds the keys it should, together they are the prefix), each build's
best-delta search against trying every k, and its segment sizes against brute force.

```
./experiments/exp2/exp2 --validate -n 512 [seed]
```
