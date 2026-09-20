# GPLA

Code and experiments for *GPLA: Robust and Dynamic Piecewise Linear Approximations for Learned Indexes*.

## Layout

- `notes/`: paper draft, experiment plan (`Notes.txt`), progress log (`workflow.md`)
- `src/ORourke/`: O'Rourke interface (`orourke.hpp`) and implementations: PGM, ZLW, brute force
- `tests/`: stress tests and speed comparison of the implementations
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
