// True sharing: every thread adds to the SAME counter. The block holding it has to move
// from core to core on every single update.
#include "roi.h"

#define ITERATIONS 20000

volatile long counter;  // volatile: really load and store it every time

int main(int argc, char** argv) {
    int id, threads;
    parse_args(argc, argv, &id, &threads);

    ROI_BEGIN();
    for (int i = 0; i < ITERATIONS; i++) counter++;
    ROI_END();

    printf("%ld\n", counter);
    return 0;
}
