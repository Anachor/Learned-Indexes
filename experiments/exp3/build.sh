#!/usr/bin/env bash
# Build experiments/exp3/exp3 with the git commit baked in, so every run's
# meta.json records the code that produced it.
#
#   experiments/exp3/build.sh          (CXX overrides the compiler)
#
# dirty is true when the code the results depend on - exp3.cpp, the shared
# experiments/common/, src/ and third_party/ - has uncommitted changes,
# untracked files included. Changes elsewhere (plots, the web server, notes) do
# not affect results, so they do not count.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
CXX="${CXX:-$(command -v g++-11 || echo g++)}"

cd "$ROOT"
COMMIT="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
if [ "$COMMIT" = unknown ]; then
    DIRTY=unknown
elif [ -n "$(git status --porcelain -- experiments/exp3/exp3.cpp experiments/common src third_party)" ]; then
    DIRTY=true
else
    DIRTY=false
fi

"$CXX" -std=c++20 -O2 -fopenmp \
    -DGIT_COMMIT="\"$COMMIT\"" -DGIT_DIRTY="\"$DIRTY\"" \
    experiments/exp3/exp3.cpp -o experiments/exp3/exp3

echo "built experiments/exp3/exp3 (commit $COMMIT, dirty $DIRTY, $("$CXX" --version | head -1))"
