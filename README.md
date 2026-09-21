# GPLA

Code and experiments for *GPLA: Robust and Dynamic Piecewise Linear Approximations for Learned Indexes*.

## Layout

- `notes/`: paper draft, experiment plan (`Notes.txt`), progress log (`workflow.md`)
- `src/ORourke/`: O'Rourke interface (`orourke.hpp`) and implementations: PGM, ZLW, brute force
- `tests/`: stress tests and speed comparison of the implementations
- `experiments/`: the experiments, writing CSVs to `results/` and plots to `figures/`
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
    g++ -std=c++17 -O2 tests/orourke.cpp -o tests/orourke
    ./tests/orourke [-v | -vv] [-i iterations] [-n MAXN] [-d MAXD] [seed]
    ```

- **Speed comparison**: Measure the speed of PGM and ZLW implementations on random cases. takes a method (PGM or ZLW), number of test cases, and maximum n as arguments. Optional: delta and seed.

    ```
    g++ -std=c++17 -O2 tests/orourke_speed.cpp -o tests/orourke_speed
    ./tests/orourke_speed -m METHOD -t T -n N [-d DELTA] [seed]
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
    ./experiments/exp1/exp1 [-n N,N,...] [-j THREADS] [--out DIR] [--permutation P] [seed]
    ```

  `--permutation` picks the insertion order of 1..n (recorded in `meta.json`):

  | P | order | optimal lambda |
  |---|---|---|
  | `uniform` (default) | uniformly random | 1 on ~95% of prefixes |
  | `zipf:R,s` | R equal key regions; each insert from a region picked with weight 1/rank^s, hot regions placed at random | > 1 on ~99% (R=16, s=1), delta ~2-8 |
  | `blocks:b` | blocks of b consecutive keys, blocks and keys in random order | > 1, but delta stuck at 0.5 |
  | `probing` | linear probing: a random key, or the next free one above it, wrapping | 1 on ~90-97%, like uniform |

  Uniform prefixes are random subsets: their deviation from a line grows like
  sqrt(length), so splitting into lambda pieces lowers delta only by sqrt(lambda)
  and one segment wins. Orders that vary the key density along the prefix -
  zipf - make splitting pay. Measured at n = 1024, 4096, 16384.

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
    ./experiments/exp1/exp1 --simulate T [--json | --segments K] [--permutation P] -n N seed
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
page says which command to run. The one exception is the **Simulate** tab, shown when
`experiments/<exp>/<exp>` is built. It runs `--simulate` with the run's seed and
permutation, read from its `meta.json` (the seed from its CSVs for runs without
one), and nothing until a button is pressed: **Simulate** plays the best segments
for t = n/8, n/4, ..., n; **Inspect prefix** shows one prefix length's table of
deltas and a zoomable plot of its segments. n is typed or picked from the run's
values. Results are cached in the page. The figures are on the **Plots** tab.
Figures are re-read on every request, so re-plotting and reloading the page is
enough to see new output.

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
