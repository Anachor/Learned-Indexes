# PMA

The packed memory array (`pma.hpp`, C++20): a sorted array with gaps, so an insert moves only the keys near it.
Insert only, keys distinct. It implements `packed_memory_array.hpp`, the interface the GPLA uses, which
reports the slots each insert changed. [Exp3](../../experiments/exp3/README.md) runs O'Rourke over its layout.

## Tests

- **Insertion workload**: by default `tests/pma` runs the insertion workload: 2^20 keys (`-n`) for each order (`--orders`, default `uniform,zipf:16,1,sorted,reverse`), each run in a new folder `results/pma/<run>/` (`<order>.csv` and `meta.json`). The plot script writes four figures to `figures/pma/<run>/overall/` - `capacity.png`, `density.png`, `moves.png` and `time.png` (keys moved and ns per insert, amortized), each against n - which the [results server](../../web/README.md) shows under the run, with the same [run modes](../../experiments/README.md#plot-modes) as exp1.
- **Stress**: `--stress` checks random insert sequences against a `std::set` and the invariants after every insert, writing nothing.

    ```
    g++-11 -std=c++20 -O3 -DGIT_COMMIT="\"$(git rev-parse --short HEAD)\"" tests/pma.cpp -o tests/pma
    ./tests/pma [-n N] [--orders O,O,...] [--out DIR] [seed]
    ./tests/pma --stress [-i iterations] [-n MAXN] [seed]
    python3 tests/plot_pma.py [--new | --latest | --all | --only=RUNS] [--results DIR] [--figures DIR]
    ```

- **Interface**: `tests/pma_api` checks the interface's contract, on `pma.hpp` and on a separate dense PMA
  that only the tests have.

    ```
    g++-11 -std=c++20 -O3 tests/pma_api.cpp -o tests/pma_api && ./tests/pma_api
    ```
