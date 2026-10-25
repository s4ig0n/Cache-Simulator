#include "cache.h"

#include <stdexcept>

static bool is_pow2(uint64_t x) { return x && !(x & (x - 1)); }

static uint32_t log2u(uint64_t x) {
    uint32_t n = 0;
    while (x > 1) { x >>= 1; ++n; }
    return n;
}

Cache::Cache(const CacheConfig& cfg, Cache* next) : cfg_(cfg), next_(next) {
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

// The line holding `block_num`, or nullptr if it isn't in the cache.
Cache::Line* Cache::find(uint64_t block_num) {
    // block number = [ tag | set index ]
    uint64_t set_index = block_num & (num_sets_ - 1);
    uint64_t tag = block_num >> index_bits_;
    Line* set = &lines_[set_index * ways_];
    for (uint32_t w = 0; w < ways_; ++w)
        if (set[w].state != Mesi::Invalid && set[w].tag == tag) return &set[w];
    return nullptr;
}

uint64_t Cache::access(uint64_t addr, bool is_write, uint32_t bytes) {
    ++clock_;
    if (is_write) ++writes_; else ++reads_;

    // address = [ tag | set index | block offset ]
    uint64_t block_num = addr >> offset_bits_;
    uint64_t set_index = block_num & (num_sets_ - 1);
    uint64_t tag = block_num >> index_bits_;

    if (Line* line = find(block_num)) {
        // ---- hit ----
        line->last_used = clock_;
        if (is_write) {
            // Shared means other cores may hold copies. They're about to be out of date,
            // so tell them to drop them before we write. (Exclusive needs no announcement:
            // nobody else has a copy.)
            if (line->state == Mesi::Shared && bus_) {
                ++upgrades_;
                bus_->broadcast(this, BusOp::Upgrade, block_num);
            }
            write(*line, addr, bytes);
        }
        return cfg_.hit_latency;  // hit: just this level's time
    }

    // ---- miss ----
    if (is_write) ++write_misses_; else ++read_misses_;
    if (invalidated_.erase(block_num)) ++coherence_misses_;  // we had it until another core took it

    // No-write-allocate: pass the write down and don't bring the block in.
    if (is_write && cfg_.alloc == AllocPolicy::NoWriteAllocate) {
        write_below(addr, bytes);
        return cfg_.hit_latency;  // the write goes into a buffer; no waiting for it
    }

    Line& victim = choose_victim(&lines_[set_index * ways_]);
    if (victim.state == Mesi::Modified) {
        // Its changes only exist here, so save the whole block before it's replaced.
        ++dirty_evictions_;
        write_below(block_addr(victim.tag, set_index), cfg_.block);
    }

    // Multi-core: announce the miss so the other caches can react. If one of them has
    // the block Modified it saves it below first, so the fetch below gets fresh data.
    bool others_have_it = false;
    if (bus_)
        others_have_it = bus_->broadcast(this, is_write ? BusOp::ReadExclusive : BusOp::Read, block_num);

    // Miss: we spent our own hit time finding out, plus however long the level below
    // takes to hand us the block.
    uint64_t cycles = cfg_.hit_latency + read_below(block_num << offset_bits_, cfg_.block);
    victim.state = others_have_it ? Mesi::Shared : Mesi::Exclusive;
    victim.tag = tag;
    victim.last_used = clock_;
    victim.filled_at = clock_;
    if (is_write) write(victim, addr, bytes);
    return cycles;
}

void Cache::write(Line& line, uint64_t addr, uint32_t bytes) {
    if (cfg_.write == WritePolicy::WriteThrough)
        write_below(addr, bytes);        // update the level below right away
    else
        line.state = Mesi::Modified;     // update it later, when the block is evicted
}

bool Cache::snoop(BusOp op, uint64_t block_num) {
    Line* line = find(block_num);
    if (!line) return false;

    // Modified = the only up-to-date copy. Save it below before anyone else reads it.
    if (line->state == Mesi::Modified) {
        ++snoop_writebacks_;
        uint64_t set_index = block_num & (num_sets_ - 1);
        write_below(block_addr(line->tag, set_index), cfg_.block);
    }

    if (op == BusOp::Read) {
        line->state = Mesi::Shared;      // someone else reads it too: keep a copy, but share it
    } else {
        line->state = Mesi::Invalid;     // someone else is about to write it: our copy will be stale
        ++invalidations_;
        invalidated_.insert(block_num);
    }
    return true;
}

bool Bus::broadcast(const Cache* from, BusOp op, uint64_t block_num) {
    if (op == BusOp::Read) ++reads_;
    else if (op == BusOp::ReadExclusive) ++read_exclusives_;
    else ++upgrades_;

    bool any = false;
    for (Cache* cache : caches_)
        if (cache != from && cache->snoop(op, block_num)) any = true;
    return any;
}

// The level below is the next cache if there is one, otherwise main memory.
// Returns how many cycles the level below took to answer.
uint64_t Cache::read_below(uint64_t addr, uint32_t bytes) {
    bytes_read_below_ += bytes;
    if (next_) return next_->access(addr, false, bytes);
    return cfg_.memory_latency;
}

// Writes going down cost no time: real CPUs drop them in a write buffer and carry on.
void Cache::write_below(uint64_t addr, uint32_t bytes) {
    bytes_written_below_ += bytes;
    if (next_) next_->access(addr, true, bytes);
}

// Rebuild a block's starting address from its tag and set: [ tag | set index | 0...0 ]
uint64_t Cache::block_addr(uint64_t tag, uint64_t set_index) const {
    return ((tag << index_bits_) | set_index) << offset_bits_;
}

void Cache::flush() {
    for (uint64_t s = 0; s < num_sets_; ++s) {
        for (uint32_t w = 0; w < ways_; ++w) {
            Line& line = lines_[s * ways_ + w];
            if (line.state == Mesi::Modified) {
                write_below(block_addr(line.tag, s), cfg_.block);
                line.state = Mesi::Exclusive;  // clean again
            }
        }
    }
}

Cache::Line& Cache::choose_victim(Line* set) {
    for (uint32_t w = 0; w < ways_; ++w)
        if (set[w].state == Mesi::Invalid) return set[w];

    // LRU evicts the line used longest ago; FIFO evicts the line filled longest ago.
    Line* victim = &set[0];
    for (uint32_t w = 1; w < ways_; ++w) {
        uint64_t age = cfg_.policy == Policy::LRU ? set[w].last_used : set[w].filled_at;
        uint64_t best = cfg_.policy == Policy::LRU ? victim->last_used : victim->filled_at;
        if (age < best) victim = &set[w];
    }
    return *victim;
}
