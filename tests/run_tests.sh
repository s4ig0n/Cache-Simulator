#!/usr/bin/env bash
# Builds cachesim and checks it against small traces whose answers were worked out by hand.
# Usage: ./tests/run_tests.sh
set -u
cd "$(dirname "$0")/.."

# MinGW on Windows may not be on PATH
command -v g++ >/dev/null 2>&1 || export PATH="/c/MinGW/bin:$PATH"

echo "Building..."
# On Windows, link statically so cachesim.exe also runs outside this script
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) STATIC=-static ;; *) STATIC= ;; esac
g++ -std=c++14 -O2 -Wall -Wextra $STATIC -o cachesim src/main.cpp src/cache.cpp src/trace.cpp || exit 1
SIM=./cachesim
[ -f cachesim.exe ] && SIM=./cachesim.exe

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0
fail=0

# check <name> <expected misses> <expected accesses> <trace file> [cachesim options...]
check() {
    local name=$1 want_misses=$2 want_accesses=$3 trace=$4
    shift 4
    local out misses accesses
    out=$("$SIM" "$@" "$trace" 2>&1)
    misses=$(echo "$out" | awk '/^misses:/ {print $2}')
    accesses=$(echo "$out" | awk '/^accesses:/ {print $2}')
    if [ "$misses" = "$want_misses" ] && [ "$accesses" = "$want_accesses" ]; then
        echo "  PASS  $name"
        pass=$((pass + 1))
    else
        echo "  FAIL  $name: expected $want_misses misses / $want_accesses accesses," \
             "got ${misses:-?} / ${accesses:-?}"
        echo "$out" | sed 's/^/        /'
        fail=$((fail + 1))
    fi
}

# A B A C A with room for 2 blocks: LRU keeps A (3 misses), FIFO evicts A (4 misses).
printf ' L 00,1\n L 10,1\n L 00,1\n L 20,1\n L 00,1\n' > "$TMP/abaca.trace"

# Reads the same block 10 times: 1 miss, then 9 hits.
for _ in 1 2 3 4 5 6 7 8 9 10; do echo " L 40,4"; done > "$TMP/repeat.trace"

# M (modify) counts as a load plus a store, and I (instruction) lines are skipped by default.
printf 'I  0400d7d4,8\n M 00,4\n' > "$TMP/modify.trace"

# check_rw <name> <reads> <writes> <read misses> <write misses> <trace file> [options...]
check_rw() {
    local name=$1 want="$2 $3 $4 $5" trace=$6
    shift 6
    local out got
    out=$("$SIM" "$@" "$trace" 2>&1)
    got=$(echo "$out" | awk '
        $1 == "reads:"                     { r  = $2 }
        $1 == "writes:"                    { w  = $2 }
        $1 == "read"  && $2 == "misses:"   { rm = $3 }
        $1 == "write" && $2 == "misses:"   { wm = $3 }
        END { print r, w, rm, wm }')
    if [ "$got" = "$want" ]; then
        echo "  PASS  $name"
        pass=$((pass + 1))
    else
        echo "  FAIL  $name: expected reads/writes/read misses/write misses = $want, got $got"
        fail=$((fail + 1))
    fi
}

# Write to a block, then read it: the write misses and brings the block in, so the read hits.
printf ' S 00,4\n L 00,4\n' > "$TMP/write_then_read.trace"

echo "Running tests..."
check "direct-mapped sample"        5 7 examples/sample.trace -s 64 -b 16 -a 1
check "2-way removes the conflict"  4 7 examples/sample.trace -s 64 -b 16 -a 2
check "LRU keeps recently used"     3 5 "$TMP/abaca.trace"    -s 32 -b 16 -a full -p lru
check "FIFO evicts oldest"          4 5 "$TMP/abaca.trace"    -s 32 -b 16 -a full -p fifo
check "repeated block hits"         1 10 "$TMP/repeat.trace"
check "modify = load + store"       1 2 "$TMP/modify.trace"
check "-i includes instructions"    2 3 "$TMP/modify.trace"   -i

# sample.trace: 6 loads + 1 store. The store (S 14) hits block 1, which L 10 just loaded.
check_rw "sample: reads vs writes"     6 1 5 0 examples/sample.trace -s 64 -b 16 -a 1
check_rw "modify = 1 read + 1 write"   1 1 1 0 "$TMP/modify.trace"
check_rw "instruction fetch is a read" 2 1 2 0 "$TMP/modify.trace" -i
check_rw "write miss, then read hit"   1 1 0 1 "$TMP/write_then_read.trace"

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
