#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

enum class Policy { LRU, FIFO };

// When a write hits: update memory now (write-through) or when the block leaves (write-back)?
enum class WritePolicy { WriteBack, WriteThrough };

// When a write misses: bring the block in first (allocate) or just send the write to memory?
enum class AllocPolicy { WriteAllocate, NoWriteAllocate };

// MESI: the state of one cache line. With a single core only Invalid, Exclusive and
// Modified ever happen (nobody to share with), which is the same as valid/dirty.
enum class Mesi {
    Invalid,    // nothing useful here
    Shared,     // clean, and other caches may have copies too
    Exclusive,  // clean, and no other cache has a copy
    Modified    // changed here; the only up-to-date copy anywhere
};

// What a cache announces on the bus before it reads or changes a block.
enum class BusOp {
    Read,           // "I want to read this block"     (a read miss)
    ReadExclusive,  // "I want to write this block"    (a write miss)
    Upgrade         // "I have it Shared, now I'm writing it: drop your copies"
};

struct CacheConfig {
    uint64_t size = 32 * 1024;  // total bytes
    uint32_t block = 64;        // bytes per block
    uint32_t assoc = 8;         // ways per set; 0 = fully associative
    Policy policy = Policy::LRU;
    WritePolicy write = WritePolicy::WriteBack;
    AllocPolicy alloc = AllocPolicy::WriteAllocate;
    uint32_t hit_latency = 4;       // cycles to answer a hit
    uint32_t memory_latency = 100;  // cycles main memory takes (only used if nothing is below)
};

class Bus;

class Cache {
public:
    // `next` is the cache below this one (e.g. L2 under L1), or nullptr if
    // the level below is main memory.
    // Throws std::invalid_argument if the configuration doesn't make sense.
    explicit Cache(const CacheConfig& cfg, Cache* next = nullptr);

    // Simulates one read or write of `bytes` bytes. Returns how many cycles it took.
    uint64_t access(uint64_t addr, bool is_write, uint32_t bytes);

    // Writes every dirty block to the level below, as happens when a program ends.
    void flush();

    // Multi-core: join a snooping bus shared with the other cores' caches.
    void connect(Bus* bus) { bus_ = bus; }

    // Another cache announced `op` for `block_num` on the bus. Reacts as MESI says
    // (save a Modified copy, downgrade to Shared, or invalidate) and returns whether
    // this cache had a copy.
    bool snoop(BusOp op, uint64_t block_num);

    uint64_t reads() const { return reads_; }
    uint64_t writes() const { return writes_; }
    uint64_t read_misses() const { return read_misses_; }
    uint64_t write_misses() const { return write_misses_; }
    uint64_t accesses() const { return reads_ + writes_; }
    uint64_t misses() const { return read_misses_ + write_misses_; }
    uint64_t hits() const { return accesses() - misses(); }

    // Traffic between this cache and the level below it (the next cache, or memory)
    uint64_t bytes_read_below() const { return bytes_read_below_; }
    uint64_t bytes_written_below() const { return bytes_written_below_; }
    uint64_t dirty_evictions() const { return dirty_evictions_; }

    // Coherence (multi-core only)
    uint64_t coherence_misses() const { return coherence_misses_; }  // missed because another core invalidated us
    uint64_t invalidations() const { return invalidations_; }        // copies we had to throw away
    uint64_t upgrades() const { return upgrades_; }                  // Shared -> Modified announcements we made
    uint64_t snoop_writebacks() const { return snoop_writebacks_; }  // Modified copies saved because another core asked

    const CacheConfig& config() const { return cfg_; }

    uint64_t num_sets() const { return num_sets_; }
    uint32_t ways() const { return ways_; }
    uint32_t offset_bits() const { return offset_bits_; }
    uint32_t index_bits() const { return index_bits_; }

private:
    struct Line {
        Mesi state = Mesi::Invalid;
        uint64_t tag = 0;
        uint64_t last_used = 0;  // for LRU
        uint64_t filled_at = 0;  // for FIFO
    };

    Line* find(uint64_t block_num);
    Line& choose_victim(Line* set);
    void write(Line& line, uint64_t addr, uint32_t bytes);
    uint64_t read_below(uint64_t addr, uint32_t bytes);
    void write_below(uint64_t addr, uint32_t bytes);
    uint64_t block_addr(uint64_t tag, uint64_t set_index) const;

    CacheConfig cfg_;
    Cache* next_;
    Bus* bus_ = nullptr;
    uint64_t num_sets_;
    uint32_t ways_;
    uint32_t offset_bits_;
    uint32_t index_bits_;
    std::vector<Line> lines_;  // num_sets_ * ways_, one set after another
    uint64_t clock_ = 0;
    uint64_t reads_ = 0;
    uint64_t writes_ = 0;
    uint64_t read_misses_ = 0;
    uint64_t write_misses_ = 0;
    uint64_t bytes_read_below_ = 0;
    uint64_t bytes_written_below_ = 0;
    uint64_t dirty_evictions_ = 0;
    uint64_t coherence_misses_ = 0;
    uint64_t invalidations_ = 0;
    uint64_t upgrades_ = 0;
    uint64_t snoop_writebacks_ = 0;
    std::unordered_set<uint64_t> invalidated_;  // blocks another core took from us
};

// The snooping bus that connects the cores' L1 caches. Every announcement goes to
// every other cache on the bus.
class Bus {
public:
    void attach(Cache* cache) { caches_.push_back(cache); }

    // Sends `op` for `block_num` from `from` to all the other caches. Returns true if
    // any of them had a copy.
    bool broadcast(const Cache* from, BusOp op, uint64_t block_num);

    uint64_t reads() const { return reads_; }
    uint64_t read_exclusives() const { return read_exclusives_; }
    uint64_t upgrades() const { return upgrades_; }
    uint64_t transactions() const { return reads_ + read_exclusives_ + upgrades_; }

private:
    std::vector<Cache*> caches_;
    uint64_t reads_ = 0;
    uint64_t read_exclusives_ = 0;
    uint64_t upgrades_ = 0;
};
