# GPLA

Code and experiments for *GPLA: Robust and Dynamic Piecewise Linear Approximations for Learned Indexes*.

## Layout

- `notes/`: paper draft, experiment plan (`Notes.txt`), progress log (`workflow.md`)
- `src/ORourke/`: O'Rourke interface (`orourke.hpp`) and implementations: PGM, ZLW, brute force
- `tests/`: stress tests and speed comparison of the implementations
- `experiments/`: the experiments, writing CSVs to `results/` and plots to `figures/`
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

- **Run**: writes one CSV per n to `results/`, with a row per delta evaluated
  (`seed,n,t,k,L,cost,fixed,best`, k = 2 delta).

    ```
    g++ -std=c++17 -O2 -fopenmp experiments/exp1.cpp -o experiments/exp1
    ./experiments/exp1 [-n N,N,...] [-j THREADS] [--out DIR] [seed]
    ```

- **Plot**: reads those CSVs, writes `exp1a_n<N>.png` and `exp1b_n<N>.png` to `figures/`.

    ```
    python3 experiments/plot_exp1.py [--results DIR] [--figures DIR]
    ```

- **Validate** (small n, writes nothing): checks the best-delta search against
  trying every delta, and the segment sizes against the brute-force O'Rourke.

    ```
    ./experiments/exp1 --validate -n 512 [seed]
    ```
