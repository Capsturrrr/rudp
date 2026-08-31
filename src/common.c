#include <string.h>
#include <stdio.h>
#include "common.h"

/*
 * Simple additive checksum (not cryptographic — this project's focus is
 * reliability/congestion logic, not corruption-proofing). Sums every byte
 * of the header (excluding the checksum field itself) and the payload,
 * then folds it into 16 bits.
 */
uint16_t rudp_checksum(const rudp_packet_t *pkt) {
    uint32_t sum = 0;

    sum += (pkt->seq_num >> 24) & 0xFF;
    sum += (pkt->seq_num >> 16) & 0xFF;
    sum += (pkt->seq_num >> 8)  & 0xFF;
    sum += (pkt->seq_num)       & 0xFF;

    sum += (pkt->ack_num >> 24) & 0xFF;
    sum += (pkt->ack_num >> 16) & 0xFF;
    sum += (pkt->ack_num >> 8)  & 0xFF;
    sum += (pkt->ack_num)       & 0xFF;

    sum += pkt->flags;

    sum += (pkt->payload_len >> 8) & 0xFF;
    sum += (pkt->payload_len)      & 0xFF;

    for (uint16_t i = 0; i < pkt->payload_len; i++) {
        sum += pkt->payload[i];
    }

    /* fold any carry into 16 bits */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)(~sum & 0xFFFF);
}

int rudp_pack(const rudp_packet_t *pkt, uint8_t *buf, size_t buf_size) {
    if (buf == NULL || pkt == NULL) return -1;

    size_t total = RUDP_HEADER_SIZE + pkt->payload_len;
    if (buf_size < total) return -1;
    if (pkt->payload_len > MAX_PAYLOAD) return -1;

    uint16_t chk = rudp_checksum(pkt);

    size_t off = 0;

    buf[off++] = (pkt->seq_num >> 24) & 0xFF;
    buf[off++] = (pkt->seq_num >> 16) & 0xFF;
    buf[off++] = (pkt->seq_num >> 8)  & 0xFF;
    buf[off++] = (pkt->seq_num)       & 0xFF;

    buf[off++] = (pkt->ack_num >> 24) & 0xFF;
    buf[off++] = (pkt->ack_num >> 16) & 0xFF;
    buf[off++] = (pkt->ack_num >> 8)  & 0xFF;
    buf[off++] = (pkt->ack_num)       & 0xFF;

    buf[off++] = pkt->flags;

    buf[off++] = (chk >> 8) & 0xFF;
    buf[off++] = (chk)      & 0xFF;

    buf[off++] = (pkt->payload_len >> 8) & 0xFF;
    buf[off++] = (pkt->payload_len)      & 0xFF;

    if (pkt->payload_len > 0) {
        memcpy(buf + off, pkt->payload, pkt->payload_len);
        off += pkt->payload_len;
    }

    return (int)off;
}

int rudp_unpack(const uint8_t *buf, size_t len, rudp_packet_t *pkt) {
    if (buf == NULL || pkt == NULL) return -1;
    if (len < RUDP_HEADER_SIZE) return -1;

    size_t off = 0;

    pkt->seq_num = ((uint32_t)buf[off]     << 24) |
                   ((uint32_t)buf[off + 1] << 16) |
                   ((uint32_t)buf[off + 2] << 8)  |
                   ((uint32_t)buf[off + 3]);
    off += 4;

    pkt->ack_num = ((uint32_t)buf[off]     << 24) |
                   ((uint32_t)buf[off + 1] << 16) |
                   ((uint32_t)buf[off + 2] << 8)  |
                   ((uint32_t)buf[off + 3]);
    off += 4;

    pkt->flags = buf[off++];

    uint16_t recv_checksum = ((uint16_t)buf[off] << 8) | buf[off + 1];
    off += 2;

    pkt->payload_len = ((uint16_t)buf[off] << 8) | buf[off + 1];
    off += 2;

    if (pkt->payload_len > MAX_PAYLOAD) return -1;
    if (len < (size_t)RUDP_HEADER_SIZE + pkt->payload_len) return -1;

    if (pkt->payload_len > 0) {
        memcpy(pkt->payload, buf + off, pkt->payload_len);
    }

    uint16_t computed_checksum = rudp_checksum(pkt);
    if (computed_checksum != recv_checksum) {
        return -2; /* checksum mismatch — corrupted packet */
    }

    return 0;
}

void rudp_print_packet(const char *label, const rudp_packet_t *pkt) {
    printf("[%s] seq=%u ack=%u flags=0x%02X (%s%s%s%s) len=%u\n",
           label,
           pkt->seq_num,
           pkt->ack_num,
           pkt->flags,
           (pkt->flags & FLAG_SYN)  ? "SYN "  : "",
           (pkt->flags & FLAG_ACK)  ? "ACK "  : "",
           (pkt->flags & FLAG_FIN)  ? "FIN "  : "",
           (pkt->flags & FLAG_DATA) ? "DATA " : "",
           pkt->payload_len);
}
