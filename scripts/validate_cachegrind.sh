#!/usr/bin/env bash
# Checks cachesim against Valgrind's own cache simulator, Cachegrind.
#
# For each workload, records a fresh Lackey trace, then for each L1 configuration runs
# Cachegrind on the same program and cachesim on the trace with the same settings, and
# compares the number of L1 data misses.
#
# Run on Linux or WSL, after ./scripts/make_traces.sh and building cachesim:
#   ./scripts/validate_cachegrind.sh
# Output: results/cachegrind_validation.csv
#
# Both tools must start the program in exactly the same way. Stack addresses depend on
# the environment (current directory, program path, environment variables), and in a
# direct-mapped cache even a small shift changes which data collide. That's why this
# script records its own trace instead of reusing traces/, which make_traces.sh recorded
# from a different directory.
#
# Only misses are compared. Cachegrind counts a modify (e.g. x++) as one access while
# cachesim counts it as a read plus a write; the write always hits, so the access totals
# differ but the miss counts should not. Only L1 is compared: Cachegrind's last-level
# cache is shared with instructions and doesn't model write-backs.
set -euo pipefail
cd "$(dirname "$0")/.."
root=$(pwd)

if ! command -v valgrind >/dev/null; then
    echo "valgrind not found. Install it with: sudo apt install valgrind" >&2
    exit 1
fi
if [ -x ./cachesim ]; then
    sim=./cachesim
elif [ -f ./cachesim.exe ]; then
    sim=./cachesim.exe   # WSL can run the Windows build directly
else
    echo "cachesim not found: build it first (make, or build.bat on Windows)" >&2
    exit 1
fi

# size in bytes, ways, block size: the same cache for both simulators.
# The last one is fully associative (256 blocks of 64 B, all in one set).
configs=(
    "32768 8 64"
    "16384 1 64"
    "8192 4 32"
    "16384 256 64"
)

out=results/cachegrind_validation.csv
mkdir -p results build/validation
echo "workload,size,ways,block,cachegrind_misses,cachesim_misses,difference_pct" > "$out"
printf "%-11s %-25s %12s %12s %10s\n" workload "L1 cache" Cachegrind cachesim difference

# Both Valgrind tools run from here, with the same absolute program path
run_dir=$(mktemp -d)
trap 'rm -rf "$run_dir"' EXIT

for src in workloads/*.c; do
    name=$(basename "$src" .c)
    if [ ! -x "build/$name" ]; then
        echo "missing build/$name: run ./scripts/make_traces.sh first" >&2
        exit 1
    fi

    # Record in Linux's /tmp (fast), then move it where cachesim can read it,
    # even the Windows build running under WSL
    (cd "$run_dir" && valgrind --tool=lackey --trace-mem=yes --log-file="$name.trace" \
        "$root/build/$name" >/dev/null)
    mv "$run_dir/$name.trace" "build/validation/$name.trace"

    for config in "${configs[@]}"; do
        read -r size ways block <<< "$config"

        # Cachegrind prints e.g. "==123== D1  misses:   48,191  ( 42,656 rd + 5,535 wr )"
        cg=$(cd "$run_dir" && valgrind --tool=cachegrind --cache-sim=yes --D1="$size,$ways,$block" \
                 --cachegrind-out-file=/dev/null "$root/build/$name" 2>&1 >/dev/null \
             | awk '/D1 +misses:/ { gsub(",", "", $4); print $4 }')

        assoc=$ways
        [ "$ways" -eq $((size / block)) ] && assoc=full
        # tr drops the Windows line endings cachesim.exe prints when run from WSL
        ours=$("$sim" -s "$size" -a "$assoc" -b "$block" "build/validation/$name.trace" \
               | tr -d '\r' | awk '$1 == "misses:" { print $2 }')

        diff=$(awk -v a="$cg" -v b="$ours" 'BEGIN { printf "%.3f", (b - a) / a * 100 }')
        if [ "$assoc" = full ]; then
            label="$((size / 1024)) KB, fully assoc., $block B"
        else
            label="$((size / 1024)) KB, $assoc-way, $block B"
        fi
        printf "%-11s %-25s %12s %12s %9s%%\n" "$name" "$label" "$cg" "$ours" "$diff"
        echo "$name,$size,$assoc,$block,$cg,$ours,$diff" >> "$out"
    done
done
echo
echo "wrote $out"
