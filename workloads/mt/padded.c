// The fix for false sharing: the same per-thread counters as false_sharing.c, but each
// one is padded out to a full 64-byte block, so no two threads ever touch the same block.
#include "roi.h"

#define ITERATIONS 20000

struct padded_counter {
    volatile long value;
    char pad[64 - sizeof(long)];  // fill the rest of the block
};

struct padded_counter counters[MAX_THREADS] __attribute__((aligned(64)));  // one block each

int main(int argc, char** argv) {
    int id, threads;
    parse_args(argc, argv, &id, &threads);

    ROI_BEGIN();
    for (int i = 0; i < ITERATIONS; i++) counters[id].value++;
    ROI_END();

    printf("%ld\n", counters[id].value);
    return 0;
}
