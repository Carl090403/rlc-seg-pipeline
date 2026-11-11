#ifndef SEGMENTER_H
#define SEGMENTER_H
#include <stddef.h>
#include <stdint.h>

/*
 * Simplified RLC-style segmentation: an SDU larger than the grant is cut into PDUs.
 * Each PDU carries a 6-byte header: SN (16 bit), segment offset (16 bit),
 * flags (bit0 = first, bit1 = last), payload length (8 bit). Big-endian on the wire.
 * This is a learning model, not a 3GPP-conformant implementation.
 */
#define SEG_HDR_LEN 6
#define SEG_MAX_PDU 255
#define SEG_MAX_SDU 9000

enum { SEG_FIRST = 1u, SEG_LAST = 2u };

typedef struct {
    uint16_t sn;
    uint16_t offset;
    uint8_t flags;
    uint8_t len;
    uint8_t payload[SEG_MAX_PDU];
} seg_pdu_t;

/* Returns number of PDUs written to out (<= max_out), or -1 on invalid arguments / not enough room. */
int seg_segment(uint16_t sn, const uint8_t *sdu, size_t sdu_len, size_t grant, seg_pdu_t *out, size_t max_out);

/* Serialize / parse one PDU. Return bytes written/consumed, or -1 on error. */
int seg_pack(const seg_pdu_t *pdu, uint8_t *buf, size_t cap);
int seg_unpack(const uint8_t *buf, size_t len, seg_pdu_t *pdu);

/* Reassembly context for one SN; PDUs may arrive out of order or duplicated. */
typedef struct {
    uint16_t sn;
    uint8_t data[SEG_MAX_SDU];
    uint8_t have[SEG_MAX_SDU]; /* byte-level receive map */
    size_t total;              /* known once the LAST segment arrived, else 0 */
    size_t received;
} seg_reasm_t;

void seg_reasm_init(seg_reasm_t *r, uint16_t sn);
/* Returns 1 when the SDU is complete, 0 if more segments are needed, -1 on a bad/inconsistent PDU. */
int seg_reasm_add(seg_reasm_t *r, const seg_pdu_t *pdu);
#endif
