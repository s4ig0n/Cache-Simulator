#include "trace.h"

#include <cctype>
#include <cstdlib>

TraceReader::TraceReader(const std::string& path) : file_(std::fopen(path.c_str(), "r")) {}

TraceReader::~TraceReader() {
    if (file_) std::fclose(file_);
}

bool TraceReader::next(Access& out) {
    char buf[256];
    while (std::fgets(buf, sizeof buf, file_)) {
        const char* p = buf;
        while (*p == ' ' || *p == '\t') ++p;

        // Blank lines, comments and Valgrind's "==1234==" banner lines.
        if (*p == '\0' || *p == '\n' || *p == '\r' || *p == '#' || *p == '=') continue;

        char op = char(std::toupper((unsigned char)*p++));
        if (op != 'I' && op != 'L' && op != 'S' && op != 'M') {
            ++skipped_lines;
            continue;
        }

        char* end;
        uint64_t addr = std::strtoull(p, &end, 16);
        if (end == p) {
            ++skipped_lines;
            continue;
        }

        uint32_t size = 1;
        if (*end == ',') size = uint32_t(std::strtoul(end + 1, nullptr, 10));

        out.op = op;
        out.addr = addr;
        out.size = size;
        return true;
    }
    return false;
}
