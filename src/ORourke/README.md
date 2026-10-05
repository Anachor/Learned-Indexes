# O'Rourke

O'Rourke's algorithm: splits points (x, y), added in order of strictly increasing x, into segments, each with
one line within ±delta of all its points. [`orourke.hpp`](orourke.hpp) is the interface, and three
implementations share it, so they are interchangeable:

- `pgm_orourke.hpp`: PGM-index's `OptimalPiecewiseLinearModel` (`third_party/PGM-index`), with the line built
  from its exact slope range - PGM's own rounds the intercept. How PGM's works:
  [notes/orourke.md](../../notes/orourke.md).
- `zlw_orourke.hpp`: ZLW's `FastPLAWorkspace` (`third_party/ZLW`).
- `brute_orourke.hpp`: brute force, for testing, in exact integer arithmetic.

The experiments use PGM's. The GPLA has its own exact O'Rourke, in
[`src/Hull/geometry.hpp`](../GPLA/README.md#hulls).

## Tests

- **Stress test**: compares PGM and ZLW implementations with a brute-force implementation on random cases. 0 mismatches so far (y = ranks only).
    ```
    g++-11 -std=c++17 -O3 tests/orourke.cpp -o tests/orourke
    ./tests/orourke [-v | -vv] [-i iterations] [-n MAXN] [-d MAXD] [seed]
    ```

- **Speed comparison**: Measure the speed of PGM and ZLW implementations on random cases. takes a method (PGM or ZLW), number of test cases, and maximum n as arguments. Optional: delta and seed.

    ```
    g++-11 -std=c++17 -O3 tests/orourke_speed.cpp -o tests/orourke_speed
    ./tests/orourke_speed -m METHOD -t T -n N [-d DELTA] [seed]
    ```
