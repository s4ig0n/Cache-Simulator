#!/usr/bin/env bash
# Compiles each program in workloads/ and records its memory trace with Valgrind.
# Run on Linux or WSL:   ./scripts/make_traces.sh
# Output: traces/<name>.trace
set -euo pipefail
cd "$(dirname "$0")/.."

if ! command -v valgrind >/dev/null; then
    echo "valgrind not found. Install it with: sudo apt install valgrind" >&2
    exit 1
fi

# Valgrind writes millions of tiny lines. Writing them straight into a Windows
# folder from WSL (/mnt/c/...) is very slow, so record into Linux's /tmp first.
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

mkdir -p build traces
for src in workloads/*.c; do
    name=$(basename "$src" .c)
    # -O1 keeps loop counters in registers, so the trace shows the program's
    # real data accesses instead of the compiler shuffling variables in memory
    gcc -O1 -o "build/$name" "$src"
    echo "recording $name..."
    valgrind --tool=lackey --trace-mem=yes --log-file="$scratch/$name.trace" "build/$name" >/dev/null
    mv "$scratch/$name.trace" "traces/$name.trace"
    echo "  traces/$name.trace  ($(wc -l < "traces/$name.trace") lines)"
done
