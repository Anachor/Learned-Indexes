# GPLA

Code and experiments for *GPLA: Robust and Dynamic Piecewise Linear Approximations for Learned Indexes*.

## Layout

- `notes/`: paper draft, experiment plan (`Notes.txt`), progress log (`workflow.md`)
- `src/ORourke/`: O'Rourke interface (`orourke.hpp`) and implementations: PGM, ZLW, brute force
- `tests/`: stress tests comparing the implementations
- `third_party/`: PGM-index (git submodule), ZLW

## Setup

```
git clone --recursive <repo-url>
# or, in an existing clone:
git submodule update --init
```

## Tests

```
g++ -std=c++17 -O2 tests/orourke.cpp -o tests/orourke
./tests/orourke [-v | -vv] [-i iterations] [-n MAXN] [-d MAXD] [seed]
```
