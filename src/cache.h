#pragma once

#include <cstdint>
#include <vector>

enum class Policy { LRU, FIFO };

// When a write hits: update memory now (write-through) or when the block leaves (write-back)?
enum class WritePolicy { WriteBack, WriteThrough };

// When a write misses: bring the block in first (allocate) or just send the write to memory?
enum class AllocPolicy { WriteAllocate, NoWriteAllocate };

struct CacheConfig {
    uint64_t size = 32 * 1024;  // total bytes
    uint32_t block = 64;        // bytes per block
    uint32_t assoc = 8;         // ways per set; 0 = fully associative
    Policy policy = Policy::LRU;
    WritePolicy write = WritePolicy::WriteBack;
    AllocPolicy alloc = AllocPolicy::WriteAllocate;
};

class Cache {
public:
    // `next` is the cache below this one (e.g. L2 under L1), or nullptr if
    // the level below is main memory.
    // Throws std::invalid_argument if the configuration doesn't make sense.
    explicit Cache(const CacheConfig& cfg, Cache* next = nullptr);

    // Simulates one read or write of `bytes` bytes. Returns true on a hit, false on a miss.
    bool access(uint64_t addr, bool is_write, uint32_t bytes);

    // Writes every dirty block to the level below, as happens when a program ends.
    void flush();

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

    const CacheConfig& config() const { return cfg_; }

    uint64_t num_sets() const { return num_sets_; }
    uint32_t ways() const { return ways_; }
    uint32_t offset_bits() const { return offset_bits_; }
    uint32_t index_bits() const { return index_bits_; }

private:
    struct Line {
        bool valid = false;
        bool dirty = false;      // changed here but not yet saved to memory (write-back only)
        uint64_t tag = 0;
        uint64_t last_used = 0;  // for LRU
        uint64_t filled_at = 0;  // for FIFO
    };

    Line& choose_victim(Line* set);
    void write(Line& line, uint64_t addr, uint32_t bytes);
    void read_below(uint64_t addr, uint32_t bytes);
    void write_below(uint64_t addr, uint32_t bytes);
    uint64_t block_addr(uint64_t tag, uint64_t set_index) const;

    CacheConfig cfg_;
    Cache* next_;
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
};
