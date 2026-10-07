#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

// One memory access from a Valgrind lackey trace, e.g. " L 7ff000398,8".
struct Access {
    char op;  // 'I' instruction fetch, 'L' load, 'S' store, 'M' modify (load + store)
    uint64_t addr;
    uint32_t size;
};

class TraceReader {
public:
    explicit TraceReader(const std::string& path);
    ~TraceReader();
    TraceReader(const TraceReader&) = delete;
    TraceReader& operator=(const TraceReader&) = delete;

    bool ok() const { return file_ != nullptr; }
    bool next(Access& out);  // false at end of file

    uint64_t skipped_lines = 0;

private:
    FILE* file_;
};
