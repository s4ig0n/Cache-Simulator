// Fills an array, then adds it up front to back: neighbouring addresses, used once.
#include <stdio.h>

#define N (256 * 1024)
static int a[N];

int main(void) {
    for (int i = 0; i < N; i++) a[i] = i % 100;
    long sum = 0;
    for (int i = 0; i < N; i++) sum += a[i];
    printf("%ld\n", sum);
    return 0;
}
