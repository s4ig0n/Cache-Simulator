#pragma once

#include <cstdint>
#include <vector>

enum class Policy { LRU, FIFO };

struct CacheConfig {
    uint64_t size = 32 * 1024;  // total bytes
    uint32_t block = 64;        // bytes per block
    uint32_t assoc = 8;         // ways per set; 0 = fully associative
    Policy policy = Policy::LRU;
};

class Cache {
public:
    // Throws std::invalid_argument if the configuration doesn't make sense.
    explicit Cache(const CacheConfig& cfg);

    // Simulates one access. Returns true on a hit, false on a miss.
    bool access(uint64_t addr);

    uint64_t hits() const { return hits_; }
    uint64_t misses() const { return misses_; }
    uint64_t accesses() const { return hits_ + misses_; }

    uint64_t num_sets() const { return num_sets_; }
    uint32_t ways() const { return ways_; }
    uint32_t offset_bits() const { return offset_bits_; }
    uint32_t index_bits() const { return index_bits_; }

private:
    struct Line {
        bool valid = false;
        uint64_t tag = 0;
        uint64_t last_used = 0;  // for LRU
        uint64_t filled_at = 0;  // for FIFO
    };

    Line& choose_victim(Line* set);

    CacheConfig cfg_;
    uint64_t num_sets_;
    uint32_t ways_;
    uint32_t offset_bits_;
    uint32_t index_bits_;
    std::vector<Line> lines_;  // num_sets_ * ways_, one set after another
    uint64_t clock_ = 0;
    uint64_t hits_ = 0;
    uint64_t misses_ = 0;
};
