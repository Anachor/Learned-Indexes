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

Query complexity, `log2(segment size) + log2(2 delta)`, of a static learned index
on a sorted array: for every prefix of a random permutation of {1..n}, the best
delta (1a) and a set of fixed deltas (1b).

- **Run**: writes one CSV per n to `results/exp1/<run>/` (run = 1, 2, ..., the next unused number; the folder is created), with a row per delta evaluated
  (`seed,n,t,k,L,cost,fixed,best`, k = 2 delta).

    ```
    g++ -std=c++17 -O2 -fopenmp experiments/exp1/exp1.cpp -o experiments/exp1/exp1
    ./experiments/exp1/exp1 [-n N,N,...] [-j THREADS] [--out DIR] [seed]
    ```

- **Plot**: reads those CSVs, writes `query_complexity.png`, `segments.png` and `best_delta.png` for each n to `figures/exp1/<run>/n<N>/`. Uses the latest run unless `--run N` is given.

    ```
    python3 experiments/exp1/plot_exp1.py [--run N] [--results DIR] [--figures DIR]
    ```

- **Validate** (small n, writes nothing): checks the best-delta search against
  trying every delta, and the segment sizes against the brute-force O'Rourke.

    ```
    ./experiments/exp1/exp1 --validate -n 512 [seed]
    ```

## Results server

Browses the figures already in `figures/`: the homepage lists the experiments, and
each experiment has a page with a run picker and an n picker showing that run's
figures, plus a link to the matching CSV. Standard library only, no dependencies.

```
python3 web/serve.py [--port N] [--root DIR]
```

Then open `http://localhost:8000/`. The selection is kept in the URL
(`/exp/exp1#run=1&n=1024`), so a particular view can be linked.

It generates nothing - run the plot script first, and if a run has no figures the
page says which command to run. Figures are re-read on every request, so
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
