// Reads from random spots in a 4 MB array: almost no address is near the last one.
#include <stdio.h>

#define N (1024 * 1024)
#define READS (256 * 1024)
// Not static: a static array that's never written is "known" to be all zeros,
// and the compiler would skip reading it entirely.
int a[N];

int main(void) {
    unsigned x = 12345;
    long sum = 0;
    for (int i = 0; i < READS; i++) {
        x = x * 1103515245u + 12345u;  // simple pseudo-random number generator
        sum += a[(x >> 8) % N];
    }
    printf("%ld\n", sum);
    return 0;
}
