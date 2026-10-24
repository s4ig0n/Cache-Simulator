#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "cache.h"
#include "trace.h"

static const char* USAGE = R"(usage: cachesim [options] <trace-file>

L1 options:
  -s SIZE    cache size, e.g. 32K, 1M     (default 32K)
  -b BYTES   block size                   (default 64)
  -a WAYS    associativity, or "full"     (default 8)
  -p POLICY  lru | fifo                   (default lru)
  -w POLICY  wb (write-back) | wt (write-through)          (default wb)
  -W POLICY  wa (write-allocate) | nwa (no-write-allocate)
             on a write miss                 (default wa for wb, nwa for wt)
  -L CYCLES  hit time                     (default 4)

L2 options (L2 is only added if you give at least one of these):
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

static void print_level(const std::string& name, const Cache& cache) {
    const CacheConfig& cfg = cache.config();
    double total = double(cache.accesses());
    double miss_rate = total ? 100.0 * cache.misses() / total : 0.0;
    // L1's lines have no prefix, so its output looks the same with or without an L2
    std::string p = name == "L1" ? "" : name + " ";

    std::cout << name << " cache:      ";
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
              << p << "dirty evictions: " << cache.dirty_evictions() << "\n\n";
}

int main(int argc, char** argv) {
    LevelOptions l1, l2;
    l2.cfg.size = 256 * 1024;
    l2.cfg.hit_latency = 12;
    uint32_t memory_latency = 100;
    bool use_l2 = false;
    bool include_instr = false;
    std::string trace_path;

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
            else trace_path = arg;
        }
        if (trace_path.empty()) { std::cerr << USAGE; return 2; }

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
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }

    // Build bottom-up: L2 first, so L1 can point at it.
    std::unique_ptr<Cache> l2_cache, l1_cache;
    try {
        if (use_l2) l2_cache.reset(new Cache(l2.cfg));
        l1_cache.reset(new Cache(l1.cfg, l2_cache.get()));
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
    Cache& cache = *l1_cache;

    TraceReader trace(trace_path);
    if (!trace.ok()) {
        std::cerr << "error: can't open " << trace_path << "\n";
        return 1;
    }

    Access a;
    uint64_t total_cycles = 0;
    while (trace.next(a)) {
        if (a.op == 'I' && !include_instr) continue;
        if (a.op == 'M') {  // modify (e.g. x++) = a read, then a write
            total_cycles += cache.access(a.addr, false, a.size);
            total_cycles += cache.access(a.addr, true, a.size);
        } else {
            // S = store = write; L and I are reads
            total_cycles += cache.access(a.addr, a.op == 'S', a.size);
        }
    }

    // The program is over: blocks still dirty must be saved, or write-back would look free.
    // Top to bottom, so L1's dirty blocks land in L2 before L2 is flushed.
    cache.flush();
    if (l2_cache) l2_cache->flush();

    print_level("L1", cache);
    if (l2_cache) print_level("L2", *l2_cache);

    // Traffic to main memory is whatever the bottom level sent below it.
    const Cache& bottom = l2_cache ? *l2_cache : cache;
    std::cout << "memory:        " << memory_latency << "-cycle access\n"
              << "memory traffic:\n"
              << "  bytes read:    " << bottom.bytes_read_below() << "\n"
              << "  bytes written: " << bottom.bytes_written_below() << "\n\n";

    // Average memory access time: total time spent waiting, divided by the number of accesses
    double amat = cache.accesses() ? double(total_cycles) / cache.accesses() : 0.0;
    std::cout << "total cycles:  " << total_cycles << "\n"
              << "AMAT:          " << std::fixed << std::setprecision(2) << amat
              << " cycles per access\n";
    if (trace.skipped_lines)
        std::cout << "(skipped " << trace.skipped_lines << " unreadable lines)\n";
    return 0;
}
