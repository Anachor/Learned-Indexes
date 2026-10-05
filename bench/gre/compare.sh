#!/usr/bin/env bash
# A first comparison of the learned PMA with GRE's indexes, single-threaded,
# under three of the paper's workloads (n = the keys used):
#
#   read-only   bulk load every key, then n lookups
#   balanced    bulk load half, then n operations: half lookups, half inserts
#   write-only  bulk load half, then insert the other half
#
#   bench/gre/compare.sh DATASET [-n N] [-o CSV] [INDEX ...]
#
# DATASET is one of GRE's (covid, genome, osm, books, fb, ...), downloaded to
# third_party/GRE/resources/ if missing and resumed if cut short, or a key
# file. -n N takes N of its keys, evenly spaced by rank (default 1M; 100k, 5M
# or a number work too; "all" for every key). Each run adds a row to CSV
# (default results/gre/<dataset>_<n>.csv); then a table of throughputs and
# memory. INDEX defaults to alex lipp pgm btree artunsync lpma; our variants
# are named as lpma-delta8, lpma-vector, lpma-leaf16 (bench/gre/lpma_index.h).
#
# Builds GRE and the sampler first (bench/gre/build.sh: quick when nothing
# changed), so the runs use the code as it is.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
GRE="$ROOT/third_party/GRE"
RESOURCES="$GRE/resources"
DATASETS="covid libio genome osm books fb history planet stack wise wiki eth gnomad planetways rev"

usage() {
    sed -n '2,21p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
}

[ $# -ge 1 ] || usage
DATASET="$1"
shift
N=1M
CSV=""
INDEXES=()
while [ $# -gt 0 ]; do
    case "$1" in
        -n) [ $# -ge 2 ] || usage; N="$2"; shift 2 ;;
        -o) [ $# -ge 2 ] || usage; CSV="$2"; shift 2 ;;
        -h | --help) usage ;;
        *) INDEXES+=("$1"); shift ;;
    esac
done
[ ${#INDEXES[@]} -gt 0 ] || INDEXES=(alex lipp pgm btree artunsync lpma)
if [[ $N =~ ^([0-9]+)([kKM]?)$ ]]; then
    case "${BASH_REMATCH[2]}" in
        k | K) N=$((10#${BASH_REMATCH[1]} * 1000)) ;;
        M) N=$((10#${BASH_REMATCH[1]} * 1000000)) ;;
        *) N=$((10#${BASH_REMATCH[1]})) ;;
    esac
elif [ "$N" != all ]; then
    echo "-n takes a number (1000000, 100k, 1M) or all" >&2
    exit 2
fi

# A key file's count (its first 8 bytes) and the keys it holds; GRE trusts the count.
count() { python3 -c 'import struct, sys; print(struct.unpack("<Q", open(sys.argv[1], "rb").read(8))[0])' "$1"; }
holds() { echo $((($(stat -c %s "$1") - 8) / 8)); }
complete() { [ -f "$1" ] && [ "$(holds "$1")" -ge "$(count "$1")" ]; }

if [ -f "$DATASET" ]; then
    SOURCE="$DATASET"
    NAME="$(basename "$DATASET")"
else
    NAME="$DATASET"
    SOURCE="$RESOURCES/$NAME"
    if ! complete "$SOURCE"; then
        if [[ " $DATASETS " != *" $NAME "* ]]; then
            echo "no key file $DATASET, and GRE has no dataset by that name ($DATASETS)" >&2
            exit 2
        fi
        echo "downloading $NAME to $RESOURCES/ (about 1.6 GB; run again to resume if it stops)"
        mkdir -p "$RESOURCES"
        wget --no-check-certificate -c -q --show-progress -P "$RESOURCES" "https://www.cse.cuhk.edu.hk/mlsys/gre/$NAME"
    fi
fi
if ! complete "$SOURCE"; then
    echo "$SOURCE says $(count "$SOURCE") keys but holds $(holds "$SOURCE"): cut short" >&2
    exit 1
fi

echo "building (log: $GRE/build.log)"
"$HERE/build.sh" >"$GRE/build.log" 2>&1 || { tail -20 "$GRE/build.log"; exit 1; }

KEYS="$SOURCE"
if [ "$N" != all ] && [ "$N" -lt "$(count "$SOURCE")" ]; then
    KEYS="${SOURCE}_$N"
    [ "$KEYS" -nt "$SOURCE" ] || "$HERE/sample" "$SOURCE" "$KEYS" "$N"
fi
N="$(count "$KEYS")"
CSV="${CSV:-$ROOT/results/gre/${NAME}_$N.csv}"
mkdir -p "$(dirname "$CSV")"
echo "$N keys from $SOURCE; rows to $CSV"

for workload in "1 0 1 read-only" "0.5 0.5 0.5 balanced" "0 1 0.5 write-only"; do
    set -- $workload  # read, insert, bulk-load fraction, name
    for index in "${INDEXES[@]}"; do
        printf '%-11s %-22s ' "$4" "$index"
        start=$SECONDS
        out="$("$GRE/build/microbench" --keys_file="$KEYS" --keys_file_type=binary --read="$1" --insert="$2" \
            --init_table_ratio="$3" --operations_num="$N" --table_size=-1 --thread_num=1 --memory \
            --index="$index" --output_path="$CSV" 2>&1)" || true
        if ! grep -q '^Throughput' <<<"$out"; then  # GRE exits 0 even on an unknown index
            echo "failed:"
            tail -5 <<<"$out"
            exit 1
        fi
        echo "$(grep '^Throughput' <<<"$out") ops/s  ($((SECONDS - start)) s)"
    done
done
echo
python3 "$HERE/table.py" "$CSV" "$KEYS"
