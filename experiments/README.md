# Experiments

Query complexity, `log2(delta) + log2(lambda)` (lambda = number of segments), at every prefix of an insertion
order of {1..n}:

| | Index | Comparison |
|---|---|---|
| [exp1](exp1/README.md) | static O'Rourke on a sorted array: the best delta (1a) and fixed deltas (1b) | |
| [exp2](exp2/README.md) | Bentley-Saxe over static O'Rourke indexes, as in the PGM paper: a fixed delta (2a) or each level's own best (2b) | exp1 |
| [exp3](exp3/README.md) | exp1 on a PMA: O'Rourke over the PMA's layout after every insert | exp1 |
| [exp4](exp4/README.md) | the GPLA, on exp3's PMA, at each delta of a grid | exp3, exp1 |

delta = k/2 for integer k, so delta covers the half-integers; the CSVs record k. `common/` holds what the
experiments share: permutations (`permutation.hpp`), the best-delta search (`segmentation.hpp`), run folders and
`meta.json` (`run.hpp`), and which runs a plot script plots (`runs.py`).

Every experiment has the same steps, all run from the repository root:

```
experiments/expN/build.sh                    # CXX=... to pick the compiler (default g++-11 if present)
./experiments/expN/expN [-n N,N,...] [--out DIR] [--permutation P] [--tiebreaker T] [seed]
python3 experiments/expN/plot_expN.py [--new | --latest | --all | --only=RUNS] [--results DIR] [--figures DIR]
./experiments/expN/expN --validate -n 512 [seed]       # small n, writes nothing
```

Each one's README has its options, CSV columns and figures.

## Runs

A run writes one CSV per n to `results/expN/<run>/` (run = 1, 2, ..., the next unused number; the folder is
created), and `meta.json`: the seed (n uses seed + n), permutation, ns, fixed deltas, cost definition, the
commit the program was built from and whether that code had uncommitted changes, the command line, start and
finish times, and per-n timing; exp3 and exp4 add their own. `status` is `running` until the run finishes, so a
stopped run stays marked as partial. Build with `build.sh` to record the commit - a plain g++ build records it
as `unknown`.

## Plot modes

Each plot script reads the CSVs and writes figures per n to `figures/expN/<run>/n<N>/`, and the across-n
summaries to `figures/expN/<run>/overall/`. Which runs:
`--new` (default) the runs with no figures, or with CSVs newer than their figures, skipping runs that have not finished;
`--latest` the highest-numbered run; `--all` every run (after changing the plot script, which `--new` cannot see);
`--only=2,3` just those runs.

## Insertion orders

`--permutation` picks the insertion order of 1..n (recorded in `meta.json`):

| P | order | behaviour |
|---|---|---|
| `uniform` (default) | uniformly random | lambda almost always 1 |
| `zipf:R,s` | R equal key regions; each insert from a region picked with weight 1/rank^s, hot regions placed at random | zipf:16,1: the most interesting - delta and lambda both vary; qc a bit higher than uniform |
| `zipf:R,s,d` | recursive zipf: the same pick d levels down (regions within regions), ranks shuffled in every region; `zipf:R,s` is d = 1 | zipf:4,1,3: delta basically 1/2, like blocks |
| `blocks:b` | blocks of b consecutive keys, blocks and keys in random order | acts like almost sorted: delta always 1/2, each segment a line through its points |
| `probing` | linear probing: a random key, or the next free one above it, wrapping | mostly like uniform, with large spikes near the end when clustering happens |
| `bitrev[:p]` | almost sorted, then bits reversed: positions in order, p·n swaps of two random positions (default 0), each position's bits reversed; needs n a power of two | lambda 1 on ~85% with delta ~1: the best case; swaps move it toward uniform (n = 512 only) |

Uniform prefixes are random subsets: their deviation from a line grows like
sqrt(length), so splitting into lambda pieces lowers delta only by sqrt(lambda)
and one segment wins. Orders that vary the key density along the prefix -
zipf - make splitting pay. Behaviour from exp1's full runs (n up to 65536); bitrev
from n = 512 only.

## Tiebreaker

`--tiebreaker minlambda|mindelta` picks the best delta when several give the same
qc (so the same delta * lambda): `minlambda`, the fewest segments and so the
largest delta (the default), or `mindelta`, the smallest delta and so the most
segments (what the runs so far used, before the option existed). qc is the same
either way; delta and lambda differ on about
10% of prefixes (n = 512). Recorded in `meta.json`, and used by `--validate` and
exp1's `--simulate` too - the results server passes the run's.
