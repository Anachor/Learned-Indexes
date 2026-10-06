#!/usr/bin/env bash
# GRE's datasets, in third_party/GRE/resources/ where compare.sh looks for
# them. Each is a binary key file: the key count, then the keys, all uint64.
#
#   bench/gre/download.sh [--status]   every dataset here and every one of GRE's:
#                                      source, keys, size, and whether complete
#   bench/gre/download.sh DATASET ...  downloads these (one cut short resumes)
#   bench/gre/download.sh --all        downloads all of GRE's (about 1.6 GB each)
#   bench/gre/download.sh --clean      deletes downloads cut short and the samples
#                                      compare.sh makes (NAME_N, made again when
#                                      needed), then --status
#
# GRE's datasets: covid libio genome osm books fb history planet stack wise
# (its paper's 10) and wiki eth gnomad planetways rev; books, fb, osm and wiki
# are SOSD's.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESOURCES="$HERE/../../third_party/GRE/resources"
URL=https://www.cse.cuhk.edu.hk/mlsys/gre
GRE_DATASETS="covid libio genome osm books fb history planet stack wise wiki eth gnomad planetways rev"
FROM_SOSD="books fb osm wiki"

usage() {
    sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
}

is_gre() { [[ " $GRE_DATASETS " == *" $1 "* ]]; }

# The key file $1's count (empty if it is shorter than one), the keys it
# holds, and whether it holds them all (GRE trusts the count).
count() { od -An -t u8 -N 8 "$1" | tr -d ' '; }
holds() {
    local bytes
    bytes="$(stat -c %s "$1")"
    echo $((bytes < 8 ? 0 : (bytes - 8) / 8))
}
complete() {
    local c
    c="$(count "$1")"
    [ -n "$c" ] && [ "$(holds "$1")" -ge "$c" ]
}

# For compare.sh's sample NAME_N (N keys of NAME), prints NAME; fails for any other file.
sample_of() {
    [[ $(basename "$1") =~ ^(.+)_([0-9]+)$ ]] || return 1
    local name="${BASH_REMATCH[1]}" n="${BASH_REMATCH[2]}"
    [ "$(count "$1")" = "$n" ] && echo "$name"
}

gre_source() { [[ " $FROM_SOSD " == *" $1 "* ]] && echo "GRE, from SOSD" || echo "GRE"; }

source_of() {
    local name parent
    name="$(basename "$1")"
    if parent="$(sample_of "$1")"; then echo "sample of $parent"
    elif is_gre "$name"; then gre_source "$name"
    elif [[ $name == *-short ]] && is_gre "${name%-short}"; then echo "first keys of ${name%-short}"
    else echo "-"
    fi
}

human() { numfmt --to=si --round=nearest --suffix=B "$1"; }

# The size of GRE's dataset $1 on its server, in bytes; empty if it cannot say.
remote_bytes() {
    wget --no-check-certificate --spider -S --timeout=10 --tries=1 "$URL/$1" 2>&1 |
        awk 'tolower($1) == "content-length:" { n = $2 + 0 } END { if (n) print n }' || true
}

row() { printf '%-22s %-22s %12s %8s  %s\n' "$@"; }

local_row() {
    local file="$1" keys state c
    if complete "$file"; then
        keys="$(count "$file")"
        state=complete
    else
        keys="$(holds "$file")"
        c="$(count "$file")"
        state="cut short${c:+: its count says $c}"
    fi
    row "$(basename "$file")" "$(source_of "$file")" "$keys" "$(human "$(stat -c %s "$file")")" "$state"
}

remote_row() {
    local bytes
    bytes="$(remote_bytes "$1")"
    if [ -n "$bytes" ]; then
        row "$1" "$(gre_source "$1")" $(((bytes - 8) / 8)) "$(human "$bytes")" "not downloaded"
    else
        row "$1" "$(gre_source "$1")" "?" "?" "not downloaded (no answer from $URL)"
    fi
}

status() {
    local name file files=0 bytes=0
    row dataset source keys size state
    for name in $GRE_DATASETS; do
        if [ -f "$RESOURCES/$name" ]; then local_row "$RESOURCES/$name"; else remote_row "$name"; fi
    done
    for file in "$RESOURCES"/*; do
        [ -f "$file" ] || continue
        is_gre "$(basename "$file")" || local_row "$file"
        files=$((files + 1))
        bytes=$((bytes + $(stat -c %s "$file")))
    done
    echo
    echo "$files files, $(human "$bytes"), in $(realpath -m "$RESOURCES"); GRE's are at $URL/<dataset>"
}

clean() {
    local file name parent
    for file in "$RESOURCES"/*; do
        [ -f "$file" ] || continue
        name="$(basename "$file")"
        if parent="$(sample_of "$file")"; then
            rm -- "$file"
            echo "deleted $name, a sample of $parent"
        elif is_gre "$name" && ! complete "$file"; then
            rm -- "$file"
            echo "deleted $name, a download cut short"
        fi
    done
    echo
}

download() {
    local dataset file
    for dataset; do
        is_gre "$dataset" || { echo "no dataset $dataset (GRE's: $GRE_DATASETS)" >&2; exit 2; }
    done
    mkdir -p "$RESOURCES"
    for dataset; do
        file="$RESOURCES/$dataset"
        if [ -f "$file" ] && complete "$file"; then
            echo "$dataset: already here"
            continue
        fi
        echo "$dataset:"
        wget --no-check-certificate -c -q --show-progress -P "$RESOURCES" "$URL/$dataset"
        complete "$file" || { echo "$file is cut short: run again to resume" >&2; exit 1; }
    done
}

case "${1:-}" in
    "" | --status) [ $# -le 1 ] || usage; status ;;
    --clean) [ $# -eq 1 ] || usage; clean; status ;;
    --all) [ $# -eq 1 ] || usage; download $GRE_DATASETS ;;
    -*) usage ;;
    *) download "$@" ;;
esac
