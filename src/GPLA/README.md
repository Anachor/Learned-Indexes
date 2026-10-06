# GPLA

The GPLA (`gpla.hpp`, C++20): a learned index on a [PMA](../PMA/README.md). Segments of keys in the PMA, each
with a line within delta of their slots, no two neighbours joinable (so at most 2 * optimal - 1 segments);
`segment.hpp` is one segment. After an insert, the segments the PMA moved are rebuilt with O'Rourke, the ones it
cut keep their unmoved parts, and neighbours are joined where one line fits. Insert only.

[Experiment 4](../../experiments/exp4/README.md) measures its query complexity, and
[bench/gre](../../bench/gre/README.md) its throughput against other indexes.

## Hulls

`src/Hull/` is what a segment keeps to test joins (`hull.hpp`): T0 `ScanHull` keeps nothing, T1 `VectorHull` its
hull as vectors, T2 `TreeHull` a tree of hulls (polylog joins and splits; searches in `chains.hpp`) whose leaves
hold up to B points each: `BasicTreeHull<B>`, `TreeHull` = B 32. All three build the same segments with the
same lines; the hull only changes the time. `by_name.hpp` names them for command lines (`tree1` ... `tree128`).
`geometry.hpp` is the exact geometry and O'Rourke.

## Tests

Each writing nothing, each `[-i iterations] [-n MAXN] [seed]`:
`tests/fitter` checks the exact O'Rourke against brute force; `tests/hulls` that T0, T1 and T2 build the same
segments, lines and hulls, joins against brute force and splits against building from scratch; `tests/gpla` the
index with all three hulls side by side on random inserts over three PMAs - after every insert its checks, the same
structure from all three, and `lower_bound`/`contains` against a `std::set`. `tests/gpla_speed` times inserts and
lookups with each hull (the median of `-r` repeats).

```
g++-11 -std=c++20 -O3 tests/fitter.cpp -o tests/fitter && ./tests/fitter
g++-11 -std=c++20 -O3 tests/hulls.cpp -o tests/hulls && ./tests/hulls
g++-11 -std=c++20 -O3 tests/gpla.cpp -o tests/gpla && ./tests/gpla
g++-11 -std=c++20 -O3 tests/gpla_speed.cpp -o tests/gpla_speed
./tests/gpla_speed [-n N,N,...] [--orders O,...] [--deltas D,...] [--hulls scan,vector,tree,tree1,...,tree128] [-q Q] [-r R] [--csv FILE]
```
