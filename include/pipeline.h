#ifndef PIPELINE_H
#define PIPELINE_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    size_t sdus_in, sdus_ok, sdus_failed;
    uint64_t pdus;
    uint32_t crc_xor; /* order-independent checksum of all reassembled SDUs */
} pipeline_stats_t;

/*
 * Runs producers -> [queue] -> N workers. Each worker segments an SDU with the given grant,
 * serialises, parses back (shuffled order), reassembles and verifies the CRC-32.
 * Returns 0 on success.
 */
int pipeline_run(size_t n_sdus, size_t sdu_len, size_t grant, int n_workers, int n_producers,
                 uint32_t seed, pipeline_stats_t *out);
#endif
