#!/usr/bin/env bash
# A first comparison of the GPLA with GRE's indexes, single-threaded,
# under three of the paper's workloads (n = the keys used):
#
#   read-only   bulk load every key, then n lookups
#   balanced    bulk load half, then n operations: half lookups, half inserts
#   write-only  bulk load half, then insert the other half
#
#   bench/gre/compare.sh DATASET[,DATASET...] [-n N] [-o CSV] [INDEX ...]
#
# Each DATASET is a key file, or a name in third_party/GRE/resources/: GRE's
# (covid, genome, osm, books, fb, ...) or the first 2M or so keys of some
# (covid-short, genome-short, history-short, libio-short, planet-short,
# wise-short). It never downloads: a dataset missing or cut short stops it
# before any run, with the wget command that gets GRE's. -n N takes N of each
# one's keys, evenly spaced by rank (default 1M; 100k, 5M or a number work too;
# "all" for every key). Each run adds a row to CSV (default, one per dataset:
# results/gre/<dataset>_<n>.csv); then a table of throughputs and memory per
# dataset. INDEX defaults to alex lipp pgm btree artunsync gpla; our variants
# are named as gpla-delta8, gpla-vector, gpla-leaf16 (bench/gre/gpla_index.h).
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
    sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
}

[ $# -ge 1 ] && [[ $1 != -h && $1 != --help ]] || usage
IFS=, read -ra NAMES <<<"$1"
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
[ ${#INDEXES[@]} -gt 0 ] || INDEXES=(alex lipp pgm btree artunsync gpla)
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

# For GRE's dataset $1, the command that downloads it (about 1.6 GB; -c resumes).
get_hint() {
    [[ " $DATASETS " == *" $1 "* ]] || return 0
    echo "get it with: wget --no-check-certificate -c -P $RESOURCES https://www.cse.cuhk.edu.hk/mlsys/gre/$1" >&2
}

SOURCES=()
for dataset in "${NAMES[@]}"; do
    source="$dataset"
    [ -f "$source" ] || source="$RESOURCES/$dataset"
    if [ ! -f "$source" ]; then
        echo "no key file $dataset, nor $source (GRE's datasets: $DATASETS)" >&2
        get_hint "$dataset"
        exit 2
    fi
    if [ "$(holds "$source")" -lt "$(count "$source")" ]; then
        echo "$source says $(count "$source") keys but holds $(holds "$source"): cut short" >&2
        get_hint "$dataset"
        exit 1
    fi
    SOURCES+=("$source")
done

echo "building (log: $GRE/build.log)"
"$HERE/build.sh" >"$GRE/build.log" 2>&1 || { tail -20 "$GRE/build.log"; exit 1; }

# Every index under each workload on N keys of the key file $1, then its table.
run_dataset() {
    local source="$1" keys="$1" n csv start out index workload
    if [ "$N" != all ] && [ "$N" -lt "$(count "$source")" ]; then
        keys="${source}_$N"
        [ "$keys" -nt "$source" ] || "$HERE/sample" "$source" "$keys" "$N"
    fi
    n="$(count "$keys")"
    csv="${CSV:-$ROOT/results/gre/$(basename "$source")_$n.csv}"
    mkdir -p "$(dirname "$csv")"
    echo
    echo "$n keys from $source; rows to $csv"

    for workload in "1 0 1 read-only" "0.5 0.5 0.5 balanced" "0 1 0.5 write-only"; do
        set -- $workload  # read, insert, bulk-load fraction, name
        for index in "${INDEXES[@]}"; do
            printf '%-11s %-22s ' "$4" "$index"
            start=$SECONDS
            out="$("$GRE/build/microbench" --keys_file="$keys" --keys_file_type=binary --read="$1" --insert="$2" \
                --init_table_ratio="$3" --operations_num="$n" --table_size=-1 --thread_num=1 --memory \
                --index="$index" --output_path="$csv" 2>&1)" || true
            if ! grep -q '^Throughput' <<<"$out"; then  # GRE exits 0 even on an unknown index
                echo "failed:"
                tail -5 <<<"$out"
                exit 1
            fi
            echo "$(grep '^Throughput' <<<"$out") ops/s  ($((SECONDS - start)) s)"
        done
    done
    echo
    python3 "$HERE/table.py" "$csv" "$keys"
}

for source in "${SOURCES[@]}"; do
    run_dataset "$source"
done
