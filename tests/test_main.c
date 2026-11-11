#include "bqueue.h"
#include "crc32.h"
#include "pipeline.h"
#include "segmenter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void test_crc32_known_vector(void) {
    CHECK(crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u); /* standard check value */
    CHECK(crc32(NULL, 0) == 0);
}

static void test_segment_boundaries(void) {
    uint8_t sdu[100];
    for (int i = 0; i < 100; i++) sdu[i] = (uint8_t)i;
    seg_pdu_t pdus[16];
    int n = seg_segment(7, sdu, sizeof sdu, 6 + 30, pdus, 16); /* 30-byte payloads */
    CHECK(n == 4);
    CHECK(pdus[0].flags == SEG_FIRST && pdus[3].flags == SEG_LAST && pdus[1].flags == 0);
    CHECK(pdus[3].len == 10 && pdus[3].offset == 90);
    n = seg_segment(1, sdu, 20, 100, pdus, 16);
    CHECK(n == 1 && pdus[0].flags == (SEG_FIRST | SEG_LAST));
    CHECK(seg_segment(1, sdu, 100, SEG_HDR_LEN, pdus, 16) == -1); /* grant too small */
    CHECK(seg_segment(1, sdu, 100, 10, pdus, 2) == -1);           /* output too small */
}

static void test_pack_unpack_roundtrip_and_errors(void) {
    seg_pdu_t p = { .sn = 0xBEEF, .offset = 300, .flags = SEG_LAST, .len = 3, .payload = { 1, 2, 3 } }, q;
    uint8_t buf[16];
    int w = seg_pack(&p, buf, sizeof buf);
    CHECK(w == 9);
    CHECK(seg_unpack(buf, (size_t)w, &q) == 9 && q.sn == 0xBEEF && q.offset == 300 && q.payload[2] == 3);
    CHECK(seg_unpack(buf, 8, &q) == -1); /* truncated */
    buf[4] = 0x80;
    CHECK(seg_unpack(buf, (size_t)w, &q) == -1); /* unknown flag bits */
    CHECK(seg_pack(&p, buf, 5) == -1);
}

static void test_reassembly_out_of_order_and_duplicates(void) {
    uint8_t sdu[50];
    for (int i = 0; i < 50; i++) sdu[i] = (uint8_t)(i * 3);
    seg_pdu_t pdus[8];
    int n = seg_segment(9, sdu, sizeof sdu, 6 + 16, pdus, 8);
    CHECK(n == 4);
    seg_reasm_t *r = malloc(sizeof *r);
    seg_reasm_init(r, 9);
    CHECK(seg_reasm_add(r, &pdus[3]) == 0);
    CHECK(seg_reasm_add(r, &pdus[1]) == 0);
    CHECK(seg_reasm_add(r, &pdus[1]) == 0); /* duplicate is harmless */
    CHECK(seg_reasm_add(r, &pdus[0]) == 0);
    CHECK(seg_reasm_add(r, &pdus[2]) == 1);
    CHECK(r->total == 50 && memcmp(r->data, sdu, 50) == 0);

    seg_reasm_init(r, 9);
    seg_pdu_t bad = pdus[1];
    bad.payload[0] ^= 0xFF;
    CHECK(seg_reasm_add(r, &pdus[1]) == 0);
    CHECK(seg_reasm_add(r, &bad) == -1); /* conflicting duplicate */
    seg_pdu_t wrong_sn = pdus[0];
    wrong_sn.sn = 10;
    CHECK(seg_reasm_add(r, &wrong_sn) == -1);
    free(r);
}

static void test_queue_close_semantics(void) {
    bqueue_t q;
    CHECK(bq_init(&q, 2) == 0);
    int a = 1, b = 2;
    void *out;
    CHECK(bq_push(&q, &a) && bq_push(&q, &b));
    bq_close(&q);
    CHECK(!bq_push(&q, &a));                    /* closed: no new items */
    CHECK(bq_pop(&q, &out) && out == &a);       /* but pending items drain in FIFO order */
    CHECK(bq_pop(&q, &out) && out == &b);
    CHECK(!bq_pop(&q, &out));
    bq_destroy(&q);
    CHECK(bq_init(&q, 0) == -1);
}

static void test_pipeline_multithreaded(void) {
    pipeline_stats_t a, b;
    CHECK(pipeline_run(400, 1500, 6 + 120, 4, 2, 42, &a) == 0);
    CHECK(a.sdus_ok == 400 && a.sdus_failed == 0 && a.pdus == 400u * 13u);
    /* Same seed, different thread counts: identical data, so identical checksum. */
    CHECK(pipeline_run(400, 1500, 6 + 120, 1, 1, 42, &b) == 0);
    CHECK(b.crc_xor == a.crc_xor);
    CHECK(pipeline_run(10, 1500, 6, 1, 1, 1, &b) == -1);
}

int main(void) {
    test_crc32_known_vector();
    test_segment_boundaries();
    test_pack_unpack_roundtrip_and_errors();
    test_reassembly_out_of_order_and_duplicates();
    test_queue_close_semantics();
    test_pipeline_multithreaded();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
