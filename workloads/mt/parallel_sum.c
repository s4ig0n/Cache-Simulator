// Data-parallel work: each thread sums its own quarter of a shared, read-only array,
// then writes its partial sum to its own padded slot. Reading shared data is cheap under
// MESI (copies can be Shared), so this should cause almost no coherence traffic.
#include "roi.h"

#define N (64 * 1024)  // 256 KB of ints

int data[N];  // not static: the compiler can't assume it's all zeros and skip the reads

struct padded_sum {
    long value;
    char pad[64 - sizeof(long)];
};
struct padded_sum partial[MAX_THREADS] __attribute__((aligned(64)));

int main(int argc, char** argv) {
    int id, threads;
    parse_args(argc, argv, &id, &threads);

    int chunk = N / threads;
    int start = id * chunk;
    int end = id == threads - 1 ? N : start + chunk;

    ROI_BEGIN();
    long sum = 0;
    for (int i = start; i < end; i++) sum += data[i];
    partial[id].value = sum;
    ROI_END();

    printf("%ld\n", partial[id].value);
    return 0;
}
