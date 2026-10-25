#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "cache.h"
#include "trace.h"

static const char* USAGE = R"(usage: cachesim [options] <trace-file> [more trace files...]

One trace file simulates one core. Several trace files simulate a multi-core CPU:
one core per file, each with its own L1, kept coherent with the MESI protocol.

L1 options (every core gets the same L1):
  -s SIZE    cache size, e.g. 32K, 1M     (default 32K)
  -b BYTES   block size                   (default 64)
  -a WAYS    associativity, or "full"     (default 8)
  -p POLICY  lru | fifo                   (default lru)
  -w POLICY  wb (write-back) | wt (write-through)          (default wb)
  -W POLICY  wa (write-allocate) | nwa (no-write-allocate)
             on a write miss                 (default wa for wb, nwa for wt)
  -L CYCLES  hit time                     (default 4)

L2 options (L2 is only added if you give at least one of these; shared by all cores):
  --l2-size SIZE     (default 256K)      --l2-policy POLICY  (default lru)
  --l2-block BYTES   (default 64)        --l2-write POLICY   (default wb)
  --l2-assoc WAYS    (default 8)         --l2-alloc POLICY
  --l2-lat CYCLES    (default 12)

Other:
  --mem-lat CYCLES  main memory access time   (default 100)
  -i                include instruction fetches ('I' lines)
)";

// "32K" -> 32768, "1M" -> 1048576, "64" -> 64
static uint64_t parse_size(const std::string& text) {
    char* end;
    uint64_t n = std::strtoull(text.c_str(), &end, 10);
    if (end == text.c_str()) throw std::invalid_argument("bad size: " + text);
    char suffix = char(std::toupper((unsigned char)*end));
    if (suffix == 'K') n <<= 10;
    else if (suffix == 'M') n <<= 20;
    else if (suffix != '\0') throw std::invalid_argument("bad size: " + text);
    return n;
}

// One cache level's settings, plus whether the user picked the allocation policy.
struct LevelOptions {
    CacheConfig cfg;
    bool alloc_given = false;
};

// Applies one setting ("size", "block", "assoc", "policy", "write" or "alloc") to a level.
static void set_option(LevelOptions& level, const std::string& key, const std::string& v) {
    CacheConfig& c = level.cfg;
    if (key == "size") c.size = parse_size(v);
    else if (key == "block") c.block = uint32_t(parse_size(v));
    else if (key == "assoc") c.assoc = v == "full" ? 0 : uint32_t(parse_size(v));
    else if (key == "lat") c.hit_latency = uint32_t(parse_size(v));
    else if (key == "policy") {
        if (v == "lru") c.policy = Policy::LRU;
        else if (v == "fifo") c.policy = Policy::FIFO;
        else throw std::invalid_argument("unknown policy: " + v);
    } else if (key == "write") {
        if (v == "wb") c.write = WritePolicy::WriteBack;
        else if (v == "wt") c.write = WritePolicy::WriteThrough;
        else throw std::invalid_argument("unknown write policy: " + v);
    } else if (key == "alloc") {
        if (v == "wa") c.alloc = AllocPolicy::WriteAllocate;
        else if (v == "nwa") c.alloc = AllocPolicy::NoWriteAllocate;
        else throw std::invalid_argument("unknown allocation policy: " + v);
        level.alloc_given = true;
    } else {
        throw std::invalid_argument("unknown option: " + key);
    }
}

// Prints one cache's numbers. Every line starts with `p` (e.g. "L2 " or "C1 ") so the
// levels and cores can be told apart; single-core L1 uses no prefix.
static void print_level(const std::string& title, const std::string& p, const Cache& cache,
                        bool coherent) {
    const CacheConfig& cfg = cache.config();
    double total = double(cache.accesses());
    double miss_rate = total ? 100.0 * cache.misses() / total : 0.0;

    std::cout << title << " cache:      ";
    if (cfg.size >= 1024) std::cout << cfg.size / 1024 << " KB, ";
    else std::cout << cfg.size << " B, ";
    std::cout << cfg.block << " B blocks, ";
    if (cache.num_sets() == 1) std::cout << "fully associative";
    else std::cout << cache.ways() << "-way, " << cache.num_sets() << " sets";
    std::cout << ", " << (cfg.policy == Policy::LRU ? "LRU" : "FIFO") << ", "
              << cfg.hit_latency << "-cycle hit\n"
              << p << "write policy:  "
              << (cfg.write == WritePolicy::WriteBack ? "write-back" : "write-through") << " + "
              << (cfg.alloc == AllocPolicy::WriteAllocate ? "write-allocate" : "no-write-allocate") << "\n"
              << p << "address:       tag | " << cache.index_bits() << " index bits | "
              << cache.offset_bits() << " offset bits\n"
              << p << "accesses:      " << cache.accesses() << "\n"
              << p << "  reads:       " << cache.reads() << "\n"
              << p << "  writes:      " << cache.writes() << "\n"
              << p << "hits:          " << cache.hits() << "\n"
              << p << "misses:        " << cache.misses() << "\n"
              << p << "  read misses: " << cache.read_misses() << "\n"
              << p << "  write misses: " << cache.write_misses() << "\n"
              << std::fixed << std::setprecision(2)
              << p << "hit rate:      " << (total ? 100.0 - miss_rate : 0.0) << "%\n"
              << p << "miss rate:     " << miss_rate << "%\n"
              << p << "dirty evictions: " << cache.dirty_evictions() << "\n";
    if (coherent)
        std::cout << p << "coherence misses: " << cache.coherence_misses() << "\n"
                  << p << "invalidations: " << cache.invalidations() << "\n"
                  << p << "upgrades:      " << cache.upgrades() << "\n"
                  << p << "snoop write-backs: " << cache.snoop_writebacks() << "\n";
    std::cout << "\n";
}

// Reads the next access this core should simulate, skipping instruction fetches unless -i.
static bool next_access(TraceReader& trace, Access& a, bool include_instr) {
    while (trace.next(a))
        if (a.op != 'I' || include_instr) return true;
    return false;
}

// Simulates one trace line on a core's L1. Returns the cycles it took.
static uint64_t simulate(Cache& cache, const Access& a) {
    if (a.op == 'M')  // modify (e.g. x++) = a read, then a write
        return cache.access(a.addr, false, a.size) + cache.access(a.addr, true, a.size);
    return cache.access(a.addr, a.op == 'S', a.size);  // S = store = write; L and I are reads
}

int main(int argc, char** argv) {
    LevelOptions l1, l2;
    l2.cfg.size = 256 * 1024;
    l2.cfg.hit_latency = 12;
    uint32_t memory_latency = 100;
    bool use_l2 = false;
    bool include_instr = false;
    std::vector<std::string> trace_paths;

    try {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            auto value = [&]() -> std::string {
                if (i + 1 >= argc) throw std::invalid_argument(arg + " needs a value");
                return argv[++i];
            };
            if (arg == "-h" || arg == "--help") { std::cout << USAGE; return 0; }
            else if (arg == "-s") set_option(l1, "size", value());
            else if (arg == "-b") set_option(l1, "block", value());
            else if (arg == "-a") set_option(l1, "assoc", value());
            else if (arg == "-p") set_option(l1, "policy", value());
            else if (arg == "-w") set_option(l1, "write", value());
            else if (arg == "-W") set_option(l1, "alloc", value());
            else if (arg == "-L") set_option(l1, "lat", value());
            else if (arg == "--mem-lat") memory_latency = uint32_t(parse_size(value()));
            else if (arg.compare(0, 5, "--l2-") == 0) {
                set_option(l2, arg.substr(5), value());
                use_l2 = true;
            } else if (arg == "-i") include_instr = true;
            else if (arg[0] == '-') throw std::invalid_argument("unknown option: " + arg);
            else trace_paths.push_back(arg);
        }
        if (trace_paths.empty()) { std::cerr << USAGE; return 2; }

        // The usual pairings: write-back + write-allocate, write-through + no-write-allocate
        for (LevelOptions* level : {&l1, &l2})
            if (!level->alloc_given)
                level->cfg.alloc = level->cfg.write == WritePolicy::WriteBack
                                       ? AllocPolicy::WriteAllocate
                                       : AllocPolicy::NoWriteAllocate;

        l1.cfg.memory_latency = memory_latency;
        l2.cfg.memory_latency = memory_latency;

        // An L1 block must fit inside one L2 block, or one L1 fetch would span two L2 blocks
        if (use_l2 && l2.cfg.block < l1.cfg.block)
            throw std::invalid_argument("L2 block size must be at least the L1 block size");

        // MESI is a protocol for write-back caches that bring blocks in on a write miss:
        // "Modified" only means something if changes stay in the cache.
        if (trace_paths.size() > 1 &&
            (l1.cfg.write != WritePolicy::WriteBack || l1.cfg.alloc != AllocPolicy::WriteAllocate))
            throw std::invalid_argument("multi-core (MESI) needs write-back, write-allocate L1 caches");
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }

    const size_t num_cores = trace_paths.size();
    const bool multicore = num_cores > 1;

    // Build bottom-up: the (shared) L2 first, so every core's L1 can point at it.
    std::unique_ptr<Cache> l2_cache;
    std::vector<std::unique_ptr<Cache>> l1s;
    Bus bus;
    try {
        if (use_l2) l2_cache.reset(new Cache(l2.cfg));
        for (size_t c = 0; c < num_cores; ++c) {
            l1s.emplace_back(new Cache(l1.cfg, l2_cache.get()));
            if (multicore) {
                l1s.back()->connect(&bus);
                bus.attach(l1s.back().get());
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }

    std::vector<std::unique_ptr<TraceReader>> traces;
    for (const std::string& path : trace_paths) {
        traces.emplace_back(new TraceReader(path));
        if (!traces.back()->ok()) {
            std::cerr << "error: can't open " << path << "\n";
            return 1;
        }
    }

    // The cores take turns, one memory access each per turn, until every trace runs out.
    // With one core this is simply "replay the trace".
    uint64_t total_cycles = 0;
    std::vector<bool> running(num_cores, true);
    size_t still_running = num_cores;
    Access a;
    while (still_running > 0) {
        for (size_t c = 0; c < num_cores; ++c) {
            if (!running[c]) continue;
            if (next_access(*traces[c], a, include_instr)) {
                total_cycles += simulate(*l1s[c], a);
            } else {
                running[c] = false;
                --still_running;
            }
        }
    }

    // The program is over: blocks still dirty must be saved, or write-back would look free.
    // Top to bottom, so L1's dirty blocks land in L2 before L2 is flushed.
    for (auto& l1_cache : l1s) l1_cache->flush();
    if (l2_cache) l2_cache->flush();

    uint64_t accesses = 0, skipped = 0;
    if (!multicore) {
        print_level("L1", "", *l1s[0], false);
    } else {
        for (size_t c = 0; c < num_cores; ++c) {
            std::string id = std::to_string(c);
            print_level("Core " + id + " L1", "C" + id + " ", *l1s[c], true);
        }
        std::cout << "bus:\n"
                  << "  reads (BusRd):           " << bus.reads() << "\n"
                  << "  read-exclusives (BusRdX): " << bus.read_exclusives() << "\n"
                  << "  upgrades (BusUpgr):      " << bus.upgrades() << "\n"
                  << "  total transactions:      " << bus.transactions() << "\n\n";
    }
    if (l2_cache) print_level("L2", "L2 ", *l2_cache, false);

    // Traffic to main memory: whatever the bottom level sent below it. Without an L2,
    // every core's L1 talks to memory directly, so add them all up.
    uint64_t mem_read = 0, mem_written = 0;
    if (l2_cache) {
        mem_read = l2_cache->bytes_read_below();
        mem_written = l2_cache->bytes_written_below();
    } else {
        for (auto& l1_cache : l1s) {
            mem_read += l1_cache->bytes_read_below();
            mem_written += l1_cache->bytes_written_below();
        }
    }
    for (auto& l1_cache : l1s) accesses += l1_cache->accesses();
    for (auto& trace : traces) skipped += trace->skipped_lines;

    std::cout << "memory:        " << memory_latency << "-cycle access\n"
              << "memory traffic:\n"
              << "  bytes read:    " << mem_read << "\n"
              << "  bytes written: " << mem_written << "\n\n";

    // Average memory access time: total time spent waiting, divided by the number of accesses
    double amat = accesses ? double(total_cycles) / accesses : 0.0;
    std::cout << "total cycles:  " << total_cycles << "\n"
              << "AMAT:          " << std::fixed << std::setprecision(2) << amat
              << " cycles per access\n";
    if (skipped)
        std::cout << "(skipped " << skipped << " unreadable lines)\n";
    return 0;
}
