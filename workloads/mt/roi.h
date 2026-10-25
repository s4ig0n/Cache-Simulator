// Shared setup for the multi-threaded workloads.
//
// Valgrind's Lackey doesn't record which thread made each access, so each "thread" runs
// as its own process: `./program <thread id> <thread count>`, doing just that thread's
// share of the work. The programs are built without PIE, so global variables sit at the
// same addresses in every run and the traces share data like real threads would.
//
// ROI_BEGIN() and ROI_END() mark the "region of interest". scripts/make_mt_traces.sh keeps
// only the trace lines between them, dropping program startup and shutdown (which every
// run would otherwise appear to share).
#ifndef ROI_H
#define ROI_H

#include <stdio.h>
#include <stdlib.h>

#define MAX_THREADS 8

volatile int roi_marker;
#define ROI_BEGIN() (roi_marker = 1)
#define ROI_END() (roi_marker = 2)

// Reads "<thread id> <thread count>" from the command line.
static void parse_args(int argc, char** argv, int* id, int* threads) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <thread id> <thread count>\n", argv[0]);
        exit(2);
    }
    *id = atoi(argv[1]);
    *threads = atoi(argv[2]);
    if (*threads < 1 || *threads > MAX_THREADS || *id < 0 || *id >= *threads) {
        fprintf(stderr, "need 0 <= id < threads <= %d\n", MAX_THREADS);
        exit(2);
    }
}

#endif
