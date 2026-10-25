// False sharing: every thread adds to ITS OWN counter, so no data is actually shared.
// But the counters are 8 bytes each and sit next to each other, so they all live in the
// same 64-byte cache block, and the block still bounces between cores like true sharing.
#include "roi.h"

#define ITERATIONS 20000

volatile long counters[MAX_THREADS] __attribute__((aligned(64)));  // all in one block

int main(int argc, char** argv) {
    int id, threads;
    parse_args(argc, argv, &id, &threads);

    ROI_BEGIN();
    for (int i = 0; i < ITERATIONS; i++) counters[id]++;
    ROI_END();

    printf("%ld\n", counters[id]);
    return 0;
}
