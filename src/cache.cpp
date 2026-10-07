#include "cache.h"

#include <stdexcept>

static bool is_pow2(uint64_t x) { return x && !(x & (x - 1)); }

static uint32_t log2u(uint64_t x) {
    uint32_t n = 0;
    while (x > 1) { x >>= 1; ++n; }
    return n;
}

Cache::Cache(const CacheConfig& cfg) : cfg_(cfg) {
    if (!is_pow2(cfg.block))
        throw std::invalid_argument("block size must be a power of two");
    if (!is_pow2(cfg.size) || cfg.size < cfg.block)
        throw std::invalid_argument("cache size must be a power of two and at least one block");

    uint64_t num_lines = cfg.size / cfg.block;
    ways_ = cfg.assoc == 0 ? uint32_t(num_lines) : cfg.assoc;
    if (!is_pow2(ways_) || ways_ > num_lines)
        throw std::invalid_argument("associativity must be a power of two no larger than the number of blocks");

    num_sets_ = num_lines / ways_;
    offset_bits_ = log2u(cfg.block);
    index_bits_ = log2u(num_sets_);
    lines_.resize(num_lines);
}

bool Cache::access(uint64_t addr) {
    ++clock_;

    // address = [ tag | set index | block offset ]
    uint64_t block_num = addr >> offset_bits_;
    uint64_t set_index = block_num & (num_sets_ - 1);
    uint64_t tag = block_num >> index_bits_;
    Line* set = &lines_[set_index * ways_];

    for (uint32_t w = 0; w < ways_; ++w) {
        if (set[w].valid && set[w].tag == tag) {
            set[w].last_used = clock_;
            ++hits_;
            return true;
        }
    }

    ++misses_;
    Line& victim = choose_victim(set);
    victim.valid = true;
    victim.tag = tag;
    victim.last_used = clock_;
    victim.filled_at = clock_;
    return false;
}

Cache::Line& Cache::choose_victim(Line* set) {
    for (uint32_t w = 0; w < ways_; ++w)
        if (!set[w].valid) return set[w];

    // LRU evicts the line used longest ago; FIFO evicts the line filled longest ago.
    Line* victim = &set[0];
    for (uint32_t w = 1; w < ways_; ++w) {
        uint64_t age = cfg_.policy == Policy::LRU ? set[w].last_used : set[w].filled_at;
        uint64_t best = cfg_.policy == Policy::LRU ? victim->last_used : victim->filled_at;
        if (age < best) victim = &set[w];
    }
    return *victim;
}
