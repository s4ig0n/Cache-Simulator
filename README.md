# Cache Simulator

A CPU cache simulator written in C++. It reads a memory trace (the list of addresses a program
accesses, as recorded by Valgrind), simulates how a cache would handle each access, and reports
the hit and miss rates.

## Features

- Configurable cache size, block size, and associativity (direct-mapped to fully associative)
- LRU and FIFO replacement
- Reads Valgrind `lackey` traces
- Separate hit/miss counts for reads (loads) and writes (stores)
- Write-back or write-through, with write-allocate or no-write-allocate, and a count of bytes moved to and from memory
- Optional second cache level (L2) with its own settings
- Sample workloads plus a script that records their traces with Valgrind

### Roadmap
- [ ] Average memory access time (AMAT)
- [ ] Experiments: miss rate vs. associativity, cache size, and block size, with graphs

## Building

```bash
make            # Linux / macOS / WSL
build.bat       # Windows (MinGW)
```

## Testing

```bash
./tests/run_tests.sh
```

Builds the simulator and runs it on small traces whose hit/miss counts were worked out by hand
(direct-mapped vs. 2-way, LRU vs. FIFO, Valgrind `M` and `I` lines).

## Usage

```
cachesim [options] <trace-file>

L1 options:
  -s SIZE    cache size, e.g. 32K, 1M     (default 32K)
  -b BYTES   block size                   (default 64)
  -a WAYS    associativity, or "full"     (default 8)
  -p POLICY  lru | fifo                   (default lru)
  -w POLICY  wb (write-back) | wt (write-through)          (default wb)
  -W POLICY  wa (write-allocate) | nwa (no-write-allocate)
             on a write miss                 (default wa for wb, nwa for wt)

L2 options (L2 is only added if you give at least one of these):
  --l2-size SIZE     (default 256K)      --l2-policy POLICY  (default lru)
  --l2-block BYTES   (default 64)        --l2-write POLICY   (default wb)
  --l2-assoc WAYS    (default 8)         --l2-alloc POLICY

Other:
  -i         include instruction fetches ('I' lines)
```

Example with a tiny direct-mapped cache:

```
$ ./cachesim -s 64 -b 16 -a 1 examples/sample.trace
L1 cache:      64 B, 16 B blocks, 1-way, 4 sets, LRU
write policy:  write-back + write-allocate
address:       tag | 2 index bits | 4 offset bits
accesses:      7
  reads:       6
  writes:      1
hits:          2
misses:        5
  read misses: 5
  write misses: 0
hit rate:      28.57%
miss rate:     71.43%
dirty evictions: 1

memory traffic:
  bytes read:    80
  bytes written: 16
```

With an L2 cache under L1:

```bash
./cachesim -s 32K --l2-size 256K traces/matmul.trace
```

## Getting traces

Traces are recorded with Valgrind on Linux or WSL (`sudo apt install valgrind`).

The `workloads/` folder has small C programs with different memory access patterns:

| Program | Access pattern |
|---|---|
| `sequential.c` | fills and sums a 1 MB array front to back |
| `random.c` | 256K reads from random spots in a 4 MB array |
| `matmul.c` | 64 x 64 matrix multiply (reads one matrix across rows, the other down columns) |
| `histogram.c` | 256K increments into 4096 counters (write-heavy) |

Record all of them into `traces/`:

```bash
./scripts/make_traces.sh
./cachesim traces/matmul.trace
```

Or trace any program of your own:

```bash
valgrind --tool=lackey --trace-mem=yes --log-file=prog.trace ./your_program
```

## How it works

Each address is split into three parts: `[ tag | set index | block offset ]`.
The set index picks which set to look in. Each line in that set is checked for a matching tag.
On a miss, an empty line is filled if there is one. Otherwise a line is evicted:
the least recently used one (LRU) or the oldest one (FIFO).
