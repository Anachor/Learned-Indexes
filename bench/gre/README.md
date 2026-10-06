# GRE benchmark

The GPLA in GRE's microbench (`third_party/GRE`, a git submodule), against GRE's indexes. `gpla_index.h` and
`gpla_index.cpp` make the [GPLA](../../src/GPLA/README.md) a GRE index, `gpla` - the tree hull with 32-point
leaves, delta = 16 - with variants named by suffixes in any order: `-delta8` (any multiple of 0.5), `-vector` or
`-scan` (another hull), `-leaf16` (the tree hull's leaf size: 1, 4, 8, 16, 32, 64, 128), so `gpla-delta8-vector`.
Single-threaded and keys only.

## Build

GRE needs TBB, jemalloc and MKL: `apt-get install libtbb-dev libjemalloc-dev libmkl-dev`.

```
bench/gre/build.sh          # CC and CXX override the compilers
```

Builds `third_party/GRE/build/microbench` and `bench/gre/sample`. `third_party/GRE` gets `gre.patch` - the gpla
indexes in `competitor.h`, and `gpla_index.cpp` compiled as C++20 in `CMakeLists.txt` - unless it has it already.

## Datasets

Downloads GRE's datasets into `third_party/GRE/resources/`, where `compare.sh` looks for them. Each is a binary
key file of about 1.6 GB (the key count, then the keys, all uint64). A complete one is skipped; one cut short is
resumed. DATASET defaults to the 10 of GRE's paper: covid libio genome osm books fb history planet stack wise.
GRE's extras: wiki eth gnomad planetways rev.

```
bench/gre/download.sh [DATASET ...]
```

## Compare

The GPLA against GRE's indexes, single-threaded, under three of GRE's paper's workloads (n = the keys used):

| workload | |
|---|---|
| read-only | bulk load every key, then n lookups |
| balanced | bulk load half, then n operations: half lookups, half inserts |
| write-only | bulk load half, then insert the other half |

```
bench/gre/compare.sh DATASET[,DATASET...] [-n N] [-o CSV] [INDEX ...]
```

Each DATASET is a key file, or a name in `third_party/GRE/resources/`: GRE's (covid, genome, osm, books, fb, ...)
or the first 2M or so keys of some (covid-short, genome-short, history-short, libio-short, planet-short,
wise-short). It never downloads: a dataset missing or cut short stops it before any run. `-n N` takes N of each
one's keys, evenly spaced by rank (default 1M; 100k, 5M or a number work too; `all` for every key). Each run adds
a row to CSV (default, one per dataset: `results/gre/<dataset>_<n>.csv`); then a table of throughputs and memory
per dataset. INDEX defaults to `alex lipp pgm btree artunsync gpla`. It runs `build.sh` first (quick when nothing
changed), so the runs use the code as it is.

The table again, from a CSV - per index, millions of operations per second under each workload, and the index's
size in MB after the write-only run; with KEYS, only that key file's rows:

```
python3 bench/gre/table.py CSV [KEYS]
```

`sample` (what `-n` uses) writes M keys of a key file, evenly spaced by rank. The shape of the distribution
stays; its fine detail does not, so local hardness (PLA at a small epsilon) is not that of the full file.

```
./bench/gre/sample IN OUT M
```
