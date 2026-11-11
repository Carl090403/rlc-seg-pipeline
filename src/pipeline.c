#include "pipeline.h"
#include "bqueue.h"
#include "crc32.h"
#include "segmenter.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint16_t sn;
    size_t len;
    uint32_t crc;
    uint8_t data[];
} job_t;

typedef struct {
    bqueue_t *q;
    size_t grant;
    pipeline_stats_t stats; /* per-worker, merged after join: no shared lock on the hot path */
    uint32_t seed;
} worker_ctx_t;

typedef struct {
    bqueue_t *q;
    size_t first, count, sdu_len;
    uint32_t seed;
} producer_ctx_t;

static uint32_t xorshift(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x ? x : 0x9E3779B9u;
}

static void *producer(void *arg) {
    producer_ctx_t *c = arg;
    for (size_t i = 0; i < c->count; i++) {
        /* SDU content depends only on (seed, SN), so results do not depend on thread counts. */
        uint32_t s = (c->seed ^ ((uint32_t)(c->first + i) * 2654435761u)) | 1u;
        job_t *j = malloc(sizeof *j + c->sdu_len);
        if (!j) break;
        j->sn = (uint16_t)(c->first + i);
        j->len = c->sdu_len;
        for (size_t b = 0; b < c->sdu_len; b++) j->data[b] = (uint8_t)xorshift(&s);
        j->crc = crc32(j->data, j->len);
        if (!bq_push(c->q, j)) { free(j); break; }
    }
    return NULL;
}

static void *worker(void *arg) {
    worker_ctx_t *c = arg;
    uint32_t s = c->seed;
    size_t max_pdus = SEG_MAX_SDU; /* worst case: 1-byte payloads */
    seg_pdu_t *pdus = malloc(max_pdus * sizeof *pdus);
    seg_reasm_t *r = malloc(sizeof *r);
    uint8_t wire[SEG_HDR_LEN + SEG_MAX_PDU];
    void *item;
    while (pdus && r && bq_pop(c->q, &item)) {
        job_t *j = item;
        int n = seg_segment(j->sn, j->data, j->len, c->grant, pdus, max_pdus);
        int ok = n > 0;
        /* Fisher-Yates shuffle to simulate out-of-order delivery. */
        for (int i = n - 1; ok && i > 0; i--) {
            int k = (int)(xorshift(&s) % (uint32_t)(i + 1));
            seg_pdu_t tmp = pdus[i]; pdus[i] = pdus[k]; pdus[k] = tmp;
        }
        seg_reasm_init(r, j->sn);
        int done = 0;
        for (int i = 0; ok && i < n; i++) {
            seg_pdu_t rx;
            int w = seg_pack(&pdus[i], wire, sizeof wire);
            ok = w > 0 && seg_unpack(wire, (size_t)w, &rx) == w;
            if (ok) { done = seg_reasm_add(r, &rx); ok = done >= 0; }
        }
        uint32_t crc = ok && done == 1 ? crc32(r->data, r->total) : 0;
        int good = ok && done == 1 && r->total == j->len && crc == j->crc;
        if (good) { c->stats.sdus_ok++; c->stats.crc_xor ^= crc; }
        else c->stats.sdus_failed++;
        c->stats.pdus += n > 0 ? (uint64_t)n : 0;
        free(j);
    }
    free(pdus);
    free(r);
    return NULL;
}

int pipeline_run(size_t n_sdus, size_t sdu_len, size_t grant, int n_workers, int n_producers,
                 uint32_t seed, pipeline_stats_t *out) {
    if (!out || n_workers < 1 || n_workers > 64 || n_producers < 1 || n_producers > 64 ||
        sdu_len == 0 || sdu_len > SEG_MAX_SDU || grant <= SEG_HDR_LEN)
        return -1;
    memset(out, 0, sizeof *out);
    out->sdus_in = n_sdus;
    crc32_init();
    bqueue_t q;
    if (bq_init(&q, 64) != 0) return -1;
    pthread_t wt[64], pt[64];
    worker_ctx_t wc[64];
    producer_ctx_t pc[64];
    for (int i = 0; i < n_workers; i++) {
        wc[i] = (worker_ctx_t){ &q, grant, { 0 }, seed * 31u + (uint32_t)i + 1u };
        pthread_create(&wt[i], NULL, worker, &wc[i]);
    }
    size_t per = n_sdus / (size_t)n_producers, rem = n_sdus % (size_t)n_producers, next = 0;
    for (int i = 0; i < n_producers; i++) {
        size_t cnt = per + ((size_t)i < rem ? 1 : 0);
        pc[i] = (producer_ctx_t){ &q, next, cnt, sdu_len, seed };
        next += cnt;
        pthread_create(&pt[i], NULL, producer, &pc[i]);
    }
    for (int i = 0; i < n_producers; i++) pthread_join(pt[i], NULL);
    bq_close(&q);
    for (int i = 0; i < n_workers; i++) {
        pthread_join(wt[i], NULL);
        out->sdus_ok += wc[i].stats.sdus_ok;
        out->sdus_failed += wc[i].stats.sdus_failed;
        out->pdus += wc[i].stats.pdus;
        out->crc_xor ^= wc[i].stats.crc_xor;
    }
    bq_destroy(&q);
    return out->sdus_ok + out->sdus_failed == n_sdus ? 0 : -1;
}
