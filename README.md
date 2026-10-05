# GPLA

Code and experiments for *GPLA: Robust and Dynamic Piecewise Linear Approximations for Learned Indexes*.

The GPLA is a dynamic learned index: segments of keys in a packed memory array (PMA), each with a line within
delta of their slots, no two neighbours joinable (so at most 2 * optimal - 1 segments). The experiments measure
its query complexity, `log2(delta) + log2(lambda)` (lambda = number of segments), at every prefix of an
insertion order, against static and Bentley-Saxe learned indexes; the GRE benchmark measures its throughput.

## Layout

| Path | What | Details |
|---|---|---|
| `src/ORourke/` | O'Rourke interface and implementations: PGM, ZLW, brute force | [README](src/ORourke/README.md) |
| `src/PMA/` | the packed memory array (C++20) | [README](src/PMA/README.md) |
| `src/GPLA/`, `src/Hull/` | the GPLA (C++20), and what each segment keeps to test joins | [README](src/GPLA/README.md) |
| `tests/` | stress tests and speed comparisons of the above | in their READMEs |
| `experiments/` | experiments 1-4, writing CSVs to `results/` and plots to `figures/` | [README](experiments/README.md) |
| `bench/gre/` | the GPLA in the GRE benchmark, against GRE's indexes | [README](bench/gre/README.md) |
| `web/` | a local server for browsing the figures in `figures/` | [README](web/README.md) |
| `notes/` | paper draft, experiment plan (`Notes.txt`), progress log (`workflow.md`), PGM's O'Rourke (`orourke.md`) | |
| `third_party/` | PGM-index and GRE (git submodules), ZLW | |

## Experiments

| | Index | |
|---|---|---|
| 1 | static O'Rourke on a sorted array: the best delta, and fixed deltas | [exp1](experiments/exp1/README.md) |
| 2 | Bentley-Saxe over static O'Rourke indexes, as in the PGM paper | [exp2](experiments/exp2/README.md) |
| 3 | static O'Rourke over a PMA the keys are inserted into | [exp3](experiments/exp3/README.md) |
| 4 | the GPLA, on exp3's PMA | [exp4](experiments/exp4/README.md) |

They share seeds and insertion orders, so the same seed and order give the same prefixes in all four, and each
plot compares against the earlier experiments' runs. What they share - run folders, `meta.json`, insertion
orders, tiebreaker, plot modes - is in [experiments/README.md](experiments/README.md).

## Setup

```
git clone --recursive <repo-url>
# or, in an existing clone:
git submodule update --init
```

The C++ builds use g++-11 (the default g++ 5 lacks `<optional>`); the build scripts pick it when present, and
`CXX=...` overrides. The plot scripts need `requirements.txt`.

Each experiment is built, run and plotted from the repository root, and the results server shows the figures:

```
experiments/exp1/build.sh
./experiments/exp1/exp1 [-n N,N,...] [--permutation P] [seed]
python3 experiments/exp1/plot_exp1.py
python3 web/serve.py                     # then http://localhost:8000/
```
