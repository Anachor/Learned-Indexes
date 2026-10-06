# Experiment 3

[Experiment 1](../exp1/README.md) on a PMA ([`src/PMA/pma.hpp`](../../src/PMA/README.md), default parameters:
leaf density 1, root 0.75, lazy growth): the keys are inserted into the PMA one at a time, and
after every insert O'Rourke runs over its layout. The points are (key, slot),
gaps included, so delta is in slots - the local search runs over the array.
Same seeds (n uses seed + n), `--permutation` orders and `--tiebreaker` as exp1,
so the same seed and order give exp1's prefixes, and exp1's static best - the
same keys packed - is the comparison. What the experiments share: [experiments/README.md](../README.md).

## Run

Writes one CSV per n to `results/exp3/<run>/`, a row per delta evaluated
(`seed,n,t,capacity,k,L,cost,fixed,best`, capacity = the PMA's after insert t),
and [`meta.json`](../README.md#runs) as exp1, with the PMA's parameters.

```
experiments/exp3/build.sh           # C++20
./experiments/exp3/exp3 [-n N,N,...] [-j THREADS] [--out DIR] [--permutation P] [--tiebreaker T] [seed]
```

## Plot

Per n, `query_complexity.png` and `segments.png` as exp1 (with exp1's
static best when an exp1 run in `--exp1-results` has the same seed and
permutation) and `best_delta.png` (best delta, its lambda, and the PMA's density);
across n, `overall/query_complexity.png`, `overall/best_delta.png` and
`overall/overhead.png` (PMA best minus static best, when every n has a match).
Same [plot modes](../README.md#plot-modes) as exp1.

```
python3 experiments/exp3/plot_exp3.py [--new | --latest | --all | --only=RUNS] [--exp1-results DIR]
```

## Validate

Small n, writes nothing: at every prefix, the PMA's invariants and
contents, the best-delta search against trying every k, and the segment sizes
against brute force.

```
./experiments/exp3/exp3 --validate -n 512 [seed]
```
