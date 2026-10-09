# Cache Simulator

A CPU cache simulator written in C++. It reads a memory trace (the list of addresses a program
accesses, as recorded by Valgrind), simulates how a cache would handle each access, and reports
the hit and miss rates.

## Features

- Configurable cache size, block size, and associativity (direct-mapped to fully associative)
- LRU and FIFO replacement
- Reads Valgrind `lackey` traces

### Roadmap
- [ ] Second cache level (L2)
- [ ] Write-back vs. write-through policies
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

  -s SIZE    cache size, e.g. 32K, 1M     (default 32K)
  -b BYTES   block size                   (default 64)
  -a WAYS    associativity, or "full"     (default 8)
  -p POLICY  lru | fifo                   (default lru)
  -i         include instruction fetches
```

Example with a tiny direct-mapped cache:

```
$ ./cachesim -s 64 -b 16 -a 1 examples/sample.trace
cache:     64 B, 16 B blocks, 1-way, 4 sets, LRU
address:   tag | 2 index bits | 4 offset bits
accesses:  7
hits:      2
misses:    5
hit rate:  28.57%
miss rate: 71.43%
```

## Getting traces

On Linux (or WSL on Windows), record any program's memory accesses with Valgrind:

```bash
valgrind --tool=lackey --trace-mem=yes --log-file=prog.trace ./your_program
./cachesim prog.trace
```

## How it works

Each address is split into three parts: `[ tag | set index | block offset ]`.
The set index picks which set to look in. Each line in that set is checked for a matching tag.
On a miss, an empty line is filled if there is one. Otherwise a line is evicted:
the least recently used one (LRU) or the oldest one (FIFO).
