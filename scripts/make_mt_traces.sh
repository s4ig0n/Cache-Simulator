#!/usr/bin/env bash
# Records one Valgrind trace per thread for each program in workloads/mt/.
# Run on Linux or WSL:   ./scripts/make_mt_traces.sh [thread count, default 4]
# Output: traces/mt/<name>.t<id>.trace, ready for:
#   cachesim traces/mt/false_sharing.t0.trace traces/mt/false_sharing.t1.trace ...
set -euo pipefail
cd "$(dirname "$0")/.."
threads=${1:-4}

if ! command -v valgrind >/dev/null; then
    echo "valgrind not found. Install it with: sudo apt install valgrind" >&2
    exit 1
fi

# Record in Linux's /tmp first: writing millions of lines straight into a Windows
# folder from WSL is very slow.
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

mkdir -p build/mt traces/mt
for src in workloads/mt/*.c; do
    name=$(basename "$src" .c)
    # -no-pie: globals get the same address in every run, so the threads' traces agree
    # on where the shared data is
    gcc -O1 -no-pie -o "build/mt/$name" "$src"

    # The address of roi_marker, without leading zeros (Lackey pads addresses, nm too)
    marker=$(nm "build/mt/$name" | awk '$3 == "roi_marker" { sub(/^0+/, "", $1); print $1 }')

    for ((t = 0; t < threads; t++)); do
        valgrind --tool=lackey --trace-mem=yes --log-file="$scratch/raw.trace" \
            "build/mt/$name" "$t" "$threads" >/dev/null
        # Keep only the lines between the two writes to roi_marker
        awk -v marker="$marker" '
            { addr = $2; sub(/,.*/, "", addr); sub(/^0+/, "", addr) }
            $1 == "S" && addr == marker { inside++; next }
            inside == 1 { print }
        ' "$scratch/raw.trace" > "$scratch/$name.t$t.trace"
        mv "$scratch/$name.t$t.trace" "traces/mt/$name.t$t.trace"
    done
    echo "traces/mt/$name.t*.trace  ($threads threads, $(wc -l < "traces/mt/$name.t0.trace") lines each)"
done
