#!/usr/bin/env bash
# Build GRE's microbench with the learned PMA in it, and the dataset sampler.
#
#   bench/gre/build.sh          (CC and CXX override the compilers)
#
# third_party/GRE gets gre.patch - the lpma indexes in competitor.h, and
# lpma_index.cpp compiled as C++20 in CMakeLists.txt - unless it has it already.
# GRE needs TBB, jemalloc and MKL: apt-get install libtbb-dev libjemalloc-dev libmkl-dev.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
GRE="$ROOT/third_party/GRE"
CC="${CC:-$(command -v gcc-11 || echo gcc)}"
CXX="${CXX:-$(command -v g++-11 || echo g++)}"

if ! git -C "$GRE" apply --reverse --check "$HERE/gre.patch" 2>/dev/null; then
    git -C "$GRE" apply "$HERE/gre.patch"
    echo "applied bench/gre/gre.patch to third_party/GRE"
fi
cmake -S "$GRE" -B "$GRE/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" >/dev/null
cmake --build "$GRE/build" -j "$(nproc)"
"$CXX" -std=c++20 -O2 "$HERE/sample.cpp" -o "$HERE/sample"

echo "built third_party/GRE/build/microbench and bench/gre/sample ($("$CXX" --version | head -1))"
