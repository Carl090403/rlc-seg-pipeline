#include "segmenter.h"
#include <string.h>

int seg_segment(uint16_t sn, const uint8_t *sdu, size_t sdu_len, size_t grant, seg_pdu_t *out, size_t max_out) {
    if (!sdu || !out || sdu_len == 0 || sdu_len > SEG_MAX_SDU || grant <= SEG_HDR_LEN) return -1;
    size_t max_payload = grant - SEG_HDR_LEN;
    if (max_payload > SEG_MAX_PDU) max_payload = SEG_MAX_PDU;
    size_t needed = (sdu_len + max_payload - 1) / max_payload;
    if (needed > max_out) return -1;
    size_t off = 0;
    for (size_t i = 0; i < needed; i++) {
        size_t n = sdu_len - off < max_payload ? sdu_len - off : max_payload;
        seg_pdu_t *p = &out[i];
        p->sn = sn;
        p->offset = (uint16_t)off;
        p->flags = (uint8_t)((i == 0 ? SEG_FIRST : 0u) | (i == needed - 1 ? SEG_LAST : 0u));
        p->len = (uint8_t)n;
        memcpy(p->payload, sdu + off, n);
        off += n;
    }
    return (int)needed;
}

int seg_pack(const seg_pdu_t *p, uint8_t *buf, size_t cap) {
    if (!p || !buf || cap < (size_t)SEG_HDR_LEN + p->len) return -1;
    buf[0] = (uint8_t)(p->sn >> 8);
    buf[1] = (uint8_t)p->sn;
    buf[2] = (uint8_t)(p->offset >> 8);
    buf[3] = (uint8_t)p->offset;
    buf[4] = p->flags;
    buf[5] = p->len;
    memcpy(buf + SEG_HDR_LEN, p->payload, p->len);
    return SEG_HDR_LEN + p->len;
}

int seg_unpack(const uint8_t *buf, size_t len, seg_pdu_t *p) {
    if (!buf || !p || len < SEG_HDR_LEN) return -1;
    p->sn = (uint16_t)((buf[0] << 8) | buf[1]);
    p->offset = (uint16_t)((buf[2] << 8) | buf[3]);
    p->flags = buf[4];
    p->len = buf[5];
    if (p->len == 0 || (p->flags & ~(SEG_FIRST | SEG_LAST)) || len < (size_t)SEG_HDR_LEN + p->len) return -1;
    memcpy(p->payload, buf + SEG_HDR_LEN, p->len);
    return SEG_HDR_LEN + p->len;
}

void seg_reasm_init(seg_reasm_t *r, uint16_t sn) {
    memset(r, 0, sizeof *r);
    r->sn = sn;
}

int seg_reasm_add(seg_reasm_t *r, const seg_pdu_t *p) {
    if (!r || !p || p->sn != r->sn || p->len == 0) return -1;
    size_t end = (size_t)p->offset + p->len;
    if (end > SEG_MAX_SDU) return -1;
    if ((p->flags & SEG_FIRST) && p->offset != 0) return -1;
    if (p->flags & SEG_LAST) {
        if (r->total && r->total != end) return -1;
        r->total = end;
    }
    if (r->total && end > r->total) return -1;
    for (size_t i = p->offset; i < end; i++) {
        if (!r->have[i]) {
            r->have[i] = 1;
            r->data[i] = p->payload[i - p->offset];
            r->received++;
        } else if (r->data[i] != p->payload[i - p->offset]) {
            return -1; /* duplicate bytes must match */
        }
    }
    return (r->total && r->received == r->total) ? 1 : 0;
}
