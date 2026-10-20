// Counts random numbers into 4096 buckets: lots of writes to the same 16 KB.
#include <stdio.h>

#define BUCKETS 4096
#define SAMPLES (256 * 1024)
static int count[BUCKETS];

int main(void) {
    unsigned x = 777;
    for (int i = 0; i < SAMPLES; i++) {
        x = x * 1103515245u + 12345u;
        count[(x >> 8) % BUCKETS]++;
    }
    printf("%d\n", count[0]);
    return 0;
}
