# GPLA

Code and experiments for *GPLA: Robust and Dynamic Piecewise Linear Approximations for Learned Indexes*.

## Layout

- `notes/`: paper draft, experiment plan (`Notes.txt`), progress log (`workflow.md`)
- `src/ORourke/`: O'Rourke interface (`orourke.hpp`) and implementations: PGM, ZLW, brute force
- `src/PMA/`: the packed memory array (`pma.hpp`, C++20)
- `src/GPLA/`: the GPLA (`gpla.hpp`, C++20): segments of keys in a PMA, each with a line within delta of
  their slots, no two neighbours joinable (so at most 2 * optimal - 1 segments); `segment.hpp` is one segment
- `src/Hull/`: what a segment keeps to test joins (`hull.hpp`): T0 `ScanHull` keeps nothing, T1 `VectorHull` its hull as
  vectors, T2 `TreeHull` a tree of hulls (polylog joins and splits; searches in `chains.hpp`) whose leaves hold up to B
  points each: `BasicTreeHull<B>`, `TreeHull` = B 32. `by_name.hpp` names them for command lines (`tree1` ...
  `tree128`). `geometry.hpp` is the exact geometry and O'Rourke
- `tests/`: stress tests and speed comparison of the implementations
- `experiments/`: the experiments, writing CSVs to `results/` and plots to `figures/`;
  `experiments/common/` holds what they share (permutations, the best-delta search, run folders and `meta.json`)
- `web/`: a local server for browsing the figures in `figures/`
- `third_party/`: PGM-index (git submodule), ZLW

## Setup

```
git clone --recursive <repo-url>
# or, in an existing clone:
git submodule update --init
```

## Tests

- **Stress test**: compares PGM and ZLW implementations with a brute-force implementation on random cases. 0 mismatches so far (y = ranks only).
    ```
    g++-11 -std=c++17 -O2 tests/orourke.cpp -o tests/orourke
    ./tests/orourke [-v | -vv] [-i iterations] [-n MAXN] [-d MAXD] [seed]
    ```

- **Speed comparison**: Measure the speed of PGM and ZLW implementations on random cases. takes a method (PGM or ZLW), number of test cases, and maximum n as arguments. Optional: delta and seed.

    ```
    g++-11 -std=c++17 -O2 tests/orourke_speed.cpp -o tests/orourke_speed
    ./tests/orourke_speed -m METHOD -t T -n N [-d DELTA] [seed]
    ```

- **PMA** (`src/PMA/pma.hpp`, C++20): by default `tests/pma` runs the insertion workload: 2^20 keys (`-n`) for each order (`--orders`, default `uniform,zipf:16,1,sorted,reverse`), each run in a new folder `results/pma/<run>/` (`<order>.csv` and `meta.json`). The plot script writes four figures to `figures/pma/<run>/overall/` - `capacity.png`, `density.png`, `moves.png` and `time.png` (keys moved and ns per insert, amortized), each against n - which the results server shows under the run, with the same run modes as exp1. `--stress` checks random insert sequences against a `std::set` and the invariants after every insert, writing nothing.
    ```
    g++-11 -std=c++20 -O2 -DGIT_COMMIT="\"$(git rev-parse --short HEAD)\"" tests/pma.cpp -o tests/pma
    ./tests/pma [-n N] [--orders O,O,...] [--out DIR] [seed]
    ./tests/pma --stress [-i iterations] [-n MAXN] [seed]
    python3 tests/plot_pma.py [--new | --latest | --all | --only=RUNS] [--results DIR] [--figures DIR]
    ```

- **GPLA** (`src/GPLA/`, `src/Hull/`, C++20), writing nothing, each `[-i iterations] [-n MAXN] [seed]`:
  `tests/fitter` checks the exact O'Rourke against brute force; `tests/hulls` that T0, T1 and T2 build the same
  segments, lines and hulls, joins against brute force and splits against building from scratch; `tests/gpla` the
  index with all three hulls side by side on random inserts over three PMAs - after every insert its checks, the same
  structure from all three, and `lower_bound`/`contains` against a `std::set`. `tests/gpla_speed` times inserts and
  lookups with each hull (the median of `-r` repeats).
    ```
    g++-11 -std=c++20 -O2 tests/fitter.cpp -o tests/fitter && ./tests/fitter
    g++-11 -std=c++20 -O2 tests/hulls.cpp -o tests/hulls && ./tests/hulls
    g++-11 -std=c++20 -O2 tests/gpla.cpp -o tests/gpla && ./tests/gpla
    g++-11 -std=c++20 -O2 tests/gpla_speed.cpp -o tests/gpla_speed
    ./tests/gpla_speed [-n N,N,...] [--orders O,...] [--deltas D,...] [--hulls scan,vector,tree,tree1,...,tree128] [-q Q] [-r R] [--csv FILE]
    ```

## Experiment 1

Query complexity, `log2(lambda) + log2(delta)` (lambda = number of segments), of a static learned index
on a sorted array: for every prefix of a random permutation of {1..n}, the best
delta (1a) and a set of fixed deltas (1b).

- **Run**: writes one CSV per n to `results/exp1/<run>/` (run = 1, 2, ..., the next unused number; the folder is created), with a row per delta evaluated
  (`seed,n,t,k,L,cost,fixed,best`, k = 2 delta, cost = log2(delta) + log2(lambda)).
  Runs before this change wrote cost as log2(2 delta) + log2(lambda), 1 more; the plot
  script recomputes it from k and L, so it handles both.

  Each run also gets `meta.json`: the seed (n uses seed + n), permutation, ns,
  fixed deltas, cost definition, the commit exp1 was built from and whether that
  code had uncommitted changes, the command line, start and finish times, and
  per-n timing. `status` is `running` until the run finishes, so a stopped run
  stays marked as partial. Build with `build.sh` to record the commit - a plain
  g++ build records it as `unknown`. Run 1 predates this; its `meta.json` was
  written afterwards from its CSVs.

    ```
    experiments/exp1/build.sh           # CXX=... to pick the compiler (default g++-11 if present)
    ./experiments/exp1/exp1 [-n N,N,...] [-j THREADS] [--out DIR] [--permutation P] [--tiebreaker T] [seed]
    ```

  `--tiebreaker minlambda|mindelta` picks the best delta when several give the same
  qc (so the same delta * lambda): `minlambda`, the fewest segments and so the
  largest delta (the default), or `mindelta`, the smallest delta and so the most
  segments (what the runs so far used, before the option existed). qc is the same
  either way; delta and lambda differ on about
  10% of prefixes (n = 512). Recorded in `meta.json`, and used by `--validate` and
  `--simulate` too - the results server passes the run's.

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
  zipf - make splitting pay. Behaviour from the full runs (n up to 65536); bitrev
  from n = 512 only.

- **Plot**: reads those CSVs, writes `query_complexity.png`, `segments.png` and `best_delta.png` for each n to `figures/exp1/<run>/n<N>/`, and the across-n summaries to `figures/exp1/<run>/overall/`. Which runs:
  `--new` (default) the runs with no figures, or with CSVs newer than their figures, skipping runs that have not finished;
  `--latest` the highest-numbered run; `--all` every run (after changing the plot script, which `--new` cannot see);
  `--only=2,3` just those runs.

    ```
    python3 experiments/exp1/plot_exp1.py [--new | --latest | --all | --only=RUNS] [--results DIR] [--figures DIR]
    ```

- **Validate** (small n, writes nothing): checks the best-delta search against
  trying every delta, and the segment sizes against the brute-force O'Rourke.

    ```
    ./experiments/exp1/exp1 --validate -n 512 [seed]
    ```

- **Simulate** one prefix (writes nothing): the prefix of length T of n, with a
  run's seed, for delta = 1/2, 1, 3/2, ... until no larger delta can lower
  qc = log2(delta) + log2(lambda). Prints `delta,lambda,qc`; `--json` adds the keys
  and the best delta's segments, `--segments K` prints only the segments for k = K
  (delta = K/2). The results server's Simulate tab runs it.

    ```
    ./experiments/exp1/exp1 --simulate T [--json | --segments K] [--permutation P] [--tiebreaker T] -n N seed
    ```

## Experiment 2

Query complexity of a dynamic learned index: Bentley-Saxe over static O'Rourke
indexes, as in the PGM paper - base 2, no buffer. Levels 0, 1, 2, ... are each
empty or hold exactly 2^i keys; inserting the t-th key merges levels 0..i-1 and the
key into the first empty level i. After t inserts the non-empty levels are the
1-bits of t. Each level is its own static index (ranks within the level), segmented
when it is built:

- 2a: a fixed delta on every level (0.5, 1, 2, ..., 32), cost = sum over levels of
  `log2(delta) + log2(lambda_i)`;
- 2b: each level with its own best delta (exp1's search), cost = sum of `log2(delta_i) + log2(lambda_i)`.

Same seeds (n uses seed + n), `--permutation` orders and `--tiebreaker` (for each level's own delta) as exp1, so the same seed
and order give exp1's prefixes, and exp1's static best is the comparison.

- **Run**: writes one CSV per n to `results/exp2/<run>/`, one row group per build -
  the prefix it was built at and the level it built, with every k evaluated on it
  (`seed,n,t,level,size,k,L,cost,fixed,best`, cost = that level's `log2(delta) + log2(lambda)`).
  The levels at prefix t are t's 1-bits, level i built at t with its low i bits
  cleared, so the plot script sums them. Also writes `meta.json`, as exp1.

    ```
    experiments/exp2/build.sh
    ./experiments/exp2/exp2 [-n N,N,...] [--out DIR] [--permutation P] [--tiebreaker T] [seed]
    ```

- **Plot**: per n, `query_complexity.png` (2a per delta, 2b, and exp1's static best
  when an exp1 run in `--exp1-results` has the same seed and permutation),
  `segments.png` (total lambda over levels) and `best_delta.png` (each build's own
  delta, one panel per level) and `average_best_delta.png` (its average per level, with exp1's best delta at t = 2^level dotted); across n, `overall/query_complexity.png` (2b, with
  exp1's static best dashed where it matches) and
  `overall/overhead.png` (2b minus static best, when every n has a match).
  Same run modes as exp1.

    ```
    python3 experiments/exp2/plot_exp2.py [--new | --latest | --all | --only=RUNS] [--exp1-results DIR]
    ```

- **Validate** (small n, writes nothing): checks the levels (sizes are t's bits,
  each holds the keys it should, together they are the prefix), each build's
  best-delta search against trying every k, and its segment sizes against brute force.

    ```
    ./experiments/exp2/exp2 --validate -n 512 [seed]
    ```

## Experiment 3

Experiment 1 on a PMA (`src/PMA/pma.hpp`, default parameters: leaf density 1,
root 0.75, lazy growth): the keys are inserted into the PMA one at a time, and
after every insert O'Rourke runs over its layout. The points are (key, slot),
gaps included, so delta is in slots - the local search runs over the array.
Same seeds (n uses seed + n), `--permutation` orders and `--tiebreaker` as exp1,
so the same seed and order give exp1's prefixes, and exp1's static best - the
same keys packed - is the comparison.

- **Run**: writes one CSV per n to `results/exp3/<run>/`, a row per delta evaluated
  (`seed,n,t,capacity,k,L,cost,fixed,best`, capacity = the PMA's after insert t),
  and `meta.json` as exp1, with the PMA's parameters.

    ```
    experiments/exp3/build.sh           # C++20
    ./experiments/exp3/exp3 [-n N,N,...] [-j THREADS] [--out DIR] [--permutation P] [--tiebreaker T] [seed]
    ```

- **Plot**: per n, `query_complexity.png` and `segments.png` as exp1 (with exp1's
  static best when an exp1 run in `--exp1-results` has the same seed and
  permutation) and `best_delta.png` (best delta, its lambda, and the PMA's density);
  across n, `overall/query_complexity.png`, `overall/best_delta.png` and
  `overall/overhead.png` (PMA best minus static best, when every n has a match).
  Same run modes as exp1.

    ```
    python3 experiments/exp3/plot_exp3.py [--new | --latest | --all | --only=RUNS] [--exp1-results DIR]
    ```

- **Validate** (small n, writes nothing): at every prefix, the PMA's invariants and
  contents, the best-delta search against trying every k, and the segment sizes
  against brute force.

    ```
    ./experiments/exp3/exp3 --validate -n 512 [seed]
    ```

## Experiment 4

Our dynamic structure, the GPLA (`src/GPLA/`): the keys are inserted into it
one at a time, once for each delta of a grid (`--deltas`, default 0.5, 1, 2, ..., 1024),
each keeping its delta throughout, and after every insert its number of segments
lambda gives the query complexity `log2(delta) + log2(lambda)`. Its PMA is exp3's, and
the seeds, `--permutation` orders and `--tiebreaker` are exp1's, so the same seed and
order give exp3's layouts: exp3's static O'Rourke on the same points is the
comparison (lambda is at most 2 * that optimum - 1, at the same delta). `--hull scan`,
`vector` or `tree` (default) only changes the time.

- **Run**: writes one CSV per n to `results/exp4/<run>/`, a row per delta and prefix
  (`seed,n,t,capacity,k,L,cost,best`, best = the grid's lowest query complexity at t),
  and `meta.json` as exp3, with the hull and, per n and delta, the inserts' time and
  work (`inserts`).

    ```
    experiments/exp4/build.sh           # C++20
    ./experiments/exp4/exp4 [-n N,N,...] [-j THREADS] [--out DIR] [--deltas D,D,...] [--hull H] [--permutation P] [--tiebreaker T] [seed]
    ```

- **Plot**: per n, `query_complexity.png` (each delta, the grid's best, exp3's static
  best over the grid and over every delta, and exp1's static best), `segments.png`,
  `optimality.png` (lambda over exp3's optimum at the same delta) and `best_delta.png`;
  across n, `overall/query_complexity.png`, `delta_qc.png` (each delta's average),
  `optimality.png`, `best_delta.png`, `overhead.png` (over exp3: what being dynamic
  costs), `overhead_exp1.png` (over the sorted array, with the PMA's part) and
  `insert_time.png`. The comparisons come from the runs in `--exp3-results` (same seed,
  permutation and PMA) and `--exp1-results` (same seed and permutation), the same
  tiebreaker preferred. Same run modes as exp1.

    ```
    python3 experiments/exp4/plot_exp4.py [--new | --latest | --all | --only=RUNS] [--exp3-results DIR] [--exp1-results DIR]
    ```

- **Validate** (small n, writes nothing): at every prefix and delta, both hulls' indexes
  pass their checks and agree, hold exp3's layout, have at most 2 * optimal - 1
  segments by exp3's O'Rourke, and answer `lower_bound` right for every key.

    ```
    ./experiments/exp4/exp4 --validate -n 512 [seed]
    ```

## Results server

Browses the figures already in `figures/`: the homepage lists the experiments, and
each experiment has a page with a run picker and an n picker showing that run's
figures, plus a link to the matching CSV. Standard library only, no dependencies.

```
python3 web/serve.py [--port N] [--root DIR]
```

Then open `http://localhost:8000/`. The selection is kept in the URL
(`/exp/exp1#run=1&n=1024`, plus `&tab=simulate` on the Simulate tab), so a particular
view can be linked.

It generates nothing - run the plot script first, and if a run has no figures the
page says which command to run. The one exception is the **Simulate** tab, shown for
experiments whose program has `--simulate` (exp1) once it is built. Pick a run, n,
start, end and a number of steps - start to end in that many equal steps, both
included (empty: start = end = n/2 and 1 step, the single prefix t = n/2; start =
end is always one prefix, and t = 0 is skipped): the
server runs `--simulate` on each of those prefix lengths with the run's seed and
permutation (from its `meta.json`, or the seed from its CSVs for runs without one)
and streams them back as they finish. The player steps through them, showing each
prefix's table of deltas tried, best marked, beside the plot of its keys and
segments; click a row to draw that delta, drag to zoom. Nothing runs until
**Simulate** is pressed. To keep the machine safe the server takes at most 100
steps per simulation and runs at most 4 simulations at once across all requests.
The tiebreaker defaults to `minlambda`, as the experiments now do, whatever the run
used (the runs so far used `mindelta`), so on a tied prefix the best delta can differ
from those runs' figures; qc does not. Pick min δ to match them.
The figures are on the **Plots** tab. Figures are re-read on every request, so
re-plotting and reloading the page is enough to see new output.

To keep it running as a systemd user service (restarts on failure, keeps running
after logout, starts at boot):

```
web/serve.sh --install [--port N]   # write the unit, enable and start it (default port 8289)
web/serve.sh --up | --down | --restart | --status
```

Re-run `--install` to change the port, or after moving the repo - it writes the
unit with this checkout's path. `--restart` after editing `serve.py`; new figures
and frontend edits only need a browser reload. `journalctl --user -u results-server -f`
follows its log.
