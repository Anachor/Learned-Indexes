#!/usr/bin/env python3
"""Per index, from GRE's CSV (bench/gre/compare.sh): millions of operations per
second under each workload, and the index's size in MB after the write-only
run. With KEYS, only that key file's rows. A run done twice shows its last row.

    python3 bench/gre/table.py CSV [KEYS]
"""

import csv
import sys

WORKLOADS = {(1.0, 0.0): "read-only", (0.5, 0.5): "balanced", (0.0, 1.0): "write-only"}


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    keys = sys.argv[2] if len(sys.argv) == 3 else None
    rows, indexes = {}, []
    with open(sys.argv[1], newline="") as f:
        for row in csv.DictReader(f):
            if keys and row["key_path"] != keys:
                continue
            workload = WORKLOADS.get((float(row["read_ratio"]), float(row["insert_ratio"])))
            if workload is None:
                continue
            if row["index_type"] not in indexes:
                indexes.append(row["index_type"])
            rows[row["index_type"], workload] = row

    print(f"{'index':<22}" + "".join(f"{w + ' Mops/s':>20}" for w in WORKLOADS.values()) + f"{'MB after writes':>17}")
    for index in indexes:
        line = f"{index:<22}"
        for workload in WORKLOADS.values():
            row = rows.get((index, workload))
            line += f"{float(row['throughput']) / 1e6:>20.3f}" if row else f"{'-':>20}"
        row = rows.get((index, "write-only"))
        line += f"{float(row['memory_consumption']) / 1e6:>17.2f}" if row else f"{'-':>17}"
        print(line)


if __name__ == "__main__":
    main()
