#include "pipeline.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int run_once(size_t n, int workers, double *ms_out) {
    struct timespec t0, t1;
    pipeline_stats_t st;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    if (pipeline_run(n, 1500, 6 + 120, workers, 2, 7, &st) != 0 || st.sdus_failed) return -1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    *ms_out = (double)(t1.tv_sec - t0.tv_sec) * 1e3 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
    return 0;
}

int main(int argc, char **argv) {
    size_t n = argc > 1 ? (size_t)atol(argv[1]) : 20000;
    double ms;

    /* Warm-up: fault in heap pages, spin up CPU clocks, prime caches.
       Result is discarded so the first measured row is not penalised. */
    if (run_once(n, 1, &ms) != 0) return 1;

    printf("%-8s %-10s %-12s\n", "workers", "ms", "SDU/s");
    for (int w = 1; w <= 8; w *= 2) {
        if (run_once(n, w, &ms) != 0) return 1;
        printf("%-8d %-10.1f %-12.0f\n", w, ms, (double)n / (ms / 1e3));
    }
    return 0;
}
