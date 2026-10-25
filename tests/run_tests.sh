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

# check_mem <name> <bytes read> <bytes written> <dirty evictions> <trace file> [options...]
check_mem() {
    local name=$1 want="$2 $3 $4" trace=$5
    shift 5
    local got
    got=$("$SIM" "$@" "$trace" 2>&1 | awk '
        $1 == "bytes" && $2 == "read:"    { r = $3 }
        $1 == "bytes" && $2 == "written:" { w = $3 }
        $1 == "dirty" && $2 == "evictions:" { d = $3 }
        END { print r, w, d }')
    if [ "$got" = "$want" ]; then
        echo "  PASS  $name"
        pass=$((pass + 1))
    else
        echo "  FAIL  $name: expected bytes read/written/dirty evictions = $want, got $got"
        fail=$((fail + 1))
    fi
}

# check_l2 <name> <L2 accesses> <L2 misses> <trace file> [options...]
check_l2() {
    local name=$1 want="$2 $3" trace=$4
    shift 4
    local got
    got=$("$SIM" "$@" "$trace" 2>&1 | awk '
        $1 == "L2" && $2 == "accesses:" { a = $3 }
        $1 == "L2" && $2 == "misses:"   { m = $3 }
        END { print a, m }')
    if [ "$got" = "$want" ]; then
        echo "  PASS  $name"
        pass=$((pass + 1))
    else
        echo "  FAIL  $name: expected L2 accesses/misses = $want, got $got"
        fail=$((fail + 1))
    fi
}

# check_amat <name> <expected AMAT> <trace file> [options...]
check_amat() {
    local name=$1 want=$2 trace=$3
    shift 3
    local got
    got=$("$SIM" "$@" "$trace" 2>&1 | awk '$1 == "AMAT:" { print $2 }')
    if [ "$got" = "$want" ]; then
        echo "  PASS  $name"
        pass=$((pass + 1))
    else
        echo "  FAIL  $name: expected AMAT $want, got ${got:-?}"
        fail=$((fail + 1))
    fi
}

# check_fields <name> <"label=value|label=value|..."> <cachesim arguments...>
# Each label is the start of an output line; its last word must equal the value.
check_fields() {
    local name=$1 spec=$2
    shift 2
    local out pair label want got problems=""
    out=$("$SIM" "$@" 2>&1)
    IFS='|' read -ra pairs <<< "$spec"
    for pair in "${pairs[@]}"; do
        label=${pair%=*}
        want=${pair##*=}
        got=$(echo "$out" | awk -v l="$label" 'index($0, l) == 1 { print $NF; exit }')
        [ "$got" = "$want" ] || problems="$problems [$label expected $want, got ${got:-?}]"
    done
    if [ -z "$problems" ]; then
        echo "  PASS  $name"
        pass=$((pass + 1))
    else
        echo "  FAIL  $name:$problems"
        fail=$((fail + 1))
    fi
}

# Ten 4-byte stores to the same address.
for _ in 1 2 3 4 5 6 7 8 9 10; do echo " S 00,4"; done > "$TMP/ten_stores.trace"

# Write block 0, then read block 4, which needs the same spot in a direct-mapped cache.
printf ' S 00,4\n L 40,4\n' > "$TMP/evict_dirty.trace"

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

# 64-byte cache, 16-byte blocks
# Write-back: fetch the block once (16), save it once at the end (16).
check_mem "write-back saves once"           16 16 0 "$TMP/ten_stores.trace" -s 64 -b 16 -a full -w wb
check      "write-back: 1 miss"             1 10    "$TMP/ten_stores.trace" -s 64 -b 16 -a full -w wb
# Write-through + no-write-allocate: never fetched, every store sent on (10 x 4 bytes).
check_mem "write-through sends every write"  0 40 0 "$TMP/ten_stores.trace" -s 64 -b 16 -a full -w wt
check      "no-write-allocate: all miss"    10 10   "$TMP/ten_stores.trace" -s 64 -b 16 -a full -w wt
# Write-through + write-allocate: fetched once, still every store sent on.
check_mem "write-through + allocate"        16 40 0 "$TMP/ten_stores.trace" -s 64 -b 16 -a full -w wt -W wa
# Write-back: block 4 evicts dirty block 0, which must be saved (16) before 2 fetches (32).
check_mem "dirty block saved on eviction"   32 16 1 "$TMP/evict_dirty.trace" -s 64 -b 16 -a 1 -w wb
# Write-through: the store goes straight to memory (4), only block 4 is fetched (16).
check_mem "write-through never dirty"       16 4 0  "$TMP/evict_dirty.trace" -s 64 -b 16 -a 1 -w wt

# L1: 64 B direct-mapped, L2: 128 B 2-way, 16-byte blocks, both write-back.
# L1 misses 5 times (00, 40, 00 again, 10, 50), and evicts dirty block 1 once.
# L2 sees those 5 fetches + 1 dirty write = 6 accesses. The second fetch of 00 hits in L2,
# because L2 has room for blocks 0 and 4 side by side. So L2 misses 4 times.
L1_L2="-s 64 -b 16 -a 1 --l2-size 128 --l2-block 16 --l2-assoc 2"
check     "L1 unchanged by adding L2"   5 7    examples/sample.trace $L1_L2
check_l2  "L2 catches an L1 miss"       6 4    examples/sample.trace $L1_L2
# Memory: 4 blocks fetched (64 B). Dirty block 1 sits in L2 until the final flush (16 B).
check_mem "memory sees only L2 misses"  64 16 1 examples/sample.trace $L1_L2
check_mem "same trace without L2"       80 16 1 examples/sample.trace -s 64 -b 16 -a 1

# AMAT (default times: L1 hit 4, L2 hit 12, memory 100 cycles)
# L1 only: 2 hits x 4 + 5 misses x (4 + 100) = 528 cycles / 7 accesses
check_amat "AMAT, L1 only"              75.43 examples/sample.trace -s 64 -b 16 -a 1
# With L2: 2 L1 hits x 4, 1 L2 hit x (4 + 12), 4 L2 misses x (4 + 12 + 100) = 488 / 7
check_amat "AMAT, L2 saves time"        69.71 examples/sample.trace $L1_L2
# Custom times: 2 x 1 + 5 x (1 + 10) = 57 / 7
check_amat "custom hit and memory time" 8.14  examples/sample.trace -s 64 -b 16 -a 1 -L 1 --mem-lat 10
# Write-back: first store misses (4 + 100), 9 hits x 4 = 140 / 10
check_amat "AMAT, write-back stores"    14.00 "$TMP/ten_stores.trace" -s 64 -b 16 -a full -w wb
# Write-through + no-write-allocate: every store misses but nobody waits for memory: 10 x 4 / 10
check_amat "writes going down are free" 4.00  "$TMP/ten_stores.trace" -s 64 -b 16 -a full -w wt

# ---- Multi-core MESI: one trace file per core, cores take turns one access at a time ----
# (default 32 KB 8-way L1 with 64-byte blocks, so addresses 00 and 08 share a block, 40 doesn't)

# Both cores read block 0. Core 0 gets it Exclusive, then core 1's read makes both Shared.
printf ' L 00,4\n' > "$TMP/c0_read.trace"
printf ' L 00,4\n' > "$TMP/c1_read.trace"
check_fields "MESI: two readers share a block" \
    "C0 misses:=1|C1 misses:=1|C0 invalidations:=0|C1 invalidations:=0|  reads (BusRd):=2|  upgrades (BusUpgr):=0" \
    "$TMP/c0_read.trace" "$TMP/c1_read.trace"

# Turn 1: core 0 reads block 0 (Exclusive). Turn 2: core 1 reads it (both Shared).
# Turn 3: core 0 writes it: a hit, but Shared, so it sends an upgrade and core 1 is invalidated.
# Turn 4: core 1 reads it again: a coherence miss. Core 0 has it Modified, so it saves it to
#         memory (a snoop write-back, 64 bytes) and both end up Shared.
printf ' L 00,4\n S 00,4\n' > "$TMP/c0_upgrade.trace"
printf ' L 00,4\n L 00,4\n' > "$TMP/c1_upgrade.trace"
check_fields "MESI: write to a shared block upgrades and invalidates" \
    "C0 misses:=1|C0 upgrades:=1|C0 snoop write-backs:=1|C1 misses:=2|C1 invalidations:=1|C1 coherence misses:=1|  reads (BusRd):=3|  upgrades (BusUpgr):=1|  bytes read:=192|  bytes written:=64" \
    "$TMP/c0_upgrade.trace" "$TMP/c1_upgrade.trace"

# Core 0 reads block 0 alone (Exclusive), then writes it: Exclusive -> Modified needs no bus
# message at all. Core 1 only touches block 1. At the end, core 0's Modified block is saved.
printf ' L 40,4\n' > "$TMP/c1_other.trace"
check_fields "MESI: writing an Exclusive block is silent" \
    "C0 upgrades:=0|  upgrades (BusUpgr):=0|  read-exclusives (BusRdX):=0|  bytes written:=64" \
    "$TMP/c0_upgrade.trace" "$TMP/c1_other.trace"

# False sharing: core 0 writes bytes 00-03, core 1 writes bytes 08-0b. Different variables,
# same 64-byte block, so every write steals the block from the other core.
# All 6 writes miss (BusRdX each). Core 0 is invalidated 3 times, core 1 twice; each
# invalidated Modified copy is saved first (5 snoop write-backs), plus core 1's final flush.
for _ in 1 2 3; do echo " S 00,4"; done > "$TMP/c0_false.trace"
for _ in 1 2 3; do echo " S 08,4"; done > "$TMP/c1_false.trace"
check_fields "MESI: false sharing ping-pongs the block" \
    "C0 misses:=3|C1 misses:=3|C0 coherence misses:=2|C1 coherence misses:=2|C0 invalidations:=3|C1 invalidations:=2|C0 snoop write-backs:=3|C1 snoop write-backs:=2|  read-exclusives (BusRdX):=6|  bytes written:=384" \
    "$TMP/c0_false.trace" "$TMP/c1_false.trace"

# The fix: put core 1's variable in a different block (byte 40). One miss each, no coherence traffic.
for _ in 1 2 3; do echo " S 40,4"; done > "$TMP/c1_padded.trace"
check_fields "MESI: padding removes false sharing" \
    "C0 misses:=1|C1 misses:=1|C0 invalidations:=0|C1 invalidations:=0|  read-exclusives (BusRdX):=2" \
    "$TMP/c0_false.trace" "$TMP/c1_padded.trace"

# MESI needs write-back caches
check_fields "MESI rejects write-through" \
    "error:=caches" \
    -w wt "$TMP/c0_false.trace" "$TMP/c1_false.trace"

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
