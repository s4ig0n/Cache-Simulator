#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

#include "cache.h"
#include "trace.h"

static const char* USAGE = R"(usage: cachesim [options] <trace-file>

options:
  -s SIZE    cache size, e.g. 32K, 1M     (default 32K)
  -b BYTES   block size                   (default 64)
  -a WAYS    associativity, or "full"     (default 8)
  -p POLICY  lru | fifo                   (default lru)
  -i         include instruction fetches ('I' lines)
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

int main(int argc, char** argv) {
    CacheConfig cfg;
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
            else if (arg == "-s") cfg.size = parse_size(value());
            else if (arg == "-b") cfg.block = uint32_t(parse_size(value()));
            else if (arg == "-a") {
                std::string v = value();
                cfg.assoc = v == "full" ? 0 : uint32_t(parse_size(v));
            } else if (arg == "-p") {
                std::string v = value();
                if (v == "lru") cfg.policy = Policy::LRU;
                else if (v == "fifo") cfg.policy = Policy::FIFO;
                else throw std::invalid_argument("unknown policy: " + v);
            } else if (arg == "-i") include_instr = true;
            else if (arg[0] == '-') throw std::invalid_argument("unknown option: " + arg);
            else trace_path = arg;
        }
        if (trace_path.empty()) { std::cerr << USAGE; return 2; }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }

    Cache cache = [&] {
        try {
            return Cache(cfg);
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << "\n";
            std::exit(2);
        }
    }();

    TraceReader trace(trace_path);
    if (!trace.ok()) {
        std::cerr << "error: can't open " << trace_path << "\n";
        return 1;
    }

    Access a;
    while (trace.next(a)) {
        if (a.op == 'I' && !include_instr) continue;
        cache.access(a.addr);
        if (a.op == 'M') cache.access(a.addr);  // modify = load then store
    }

    double total = double(cache.accesses());
    double hit_rate = total ? 100.0 * cache.hits() / total : 0.0;

    std::cout << "cache:     ";
    if (cfg.size >= 1024) std::cout << cfg.size / 1024 << " KB, ";
    else std::cout << cfg.size << " B, ";
    std::cout << cfg.block << " B blocks, ";
    if (cache.num_sets() == 1) std::cout << "fully associative";
    else std::cout << cache.ways() << "-way, " << cache.num_sets() << " sets";
    std::cout << ", " << (cfg.policy == Policy::LRU ? "LRU" : "FIFO") << "\n"
              << "address:   tag | " << cache.index_bits() << " index bits | "
              << cache.offset_bits() << " offset bits\n"
              << "accesses:  " << cache.accesses() << "\n"
              << "hits:      " << cache.hits() << "\n"
              << "misses:    " << cache.misses() << "\n"
              << std::fixed << std::setprecision(2)
              << "hit rate:  " << hit_rate << "%\n"
              << "miss rate: " << (total ? 100.0 - hit_rate : 0.0) << "%\n";
    if (trace.skipped_lines)
        std::cout << "(skipped " << trace.skipped_lines << " unreadable lines)\n";
    return 0;
}
