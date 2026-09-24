#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stddef.h>

#define SERVER_PORT 8888
#define SERVER_IP "127.0.0.1"
#define BUFFER_SIZE 1024
#define MAX_PAYLOAD 512

/* Flag bits — a packet can carry more than one, e.g. SYN+ACK */
#define FLAG_SYN  0x01
#define FLAG_ACK  0x02
#define FLAG_FIN  0x04
#define FLAG_DATA 0x08

/* Phase 4: reliable delivery tuning */
#define WINDOW_SIZE 4          /* max unacknowledged packets in flight */
#define TIMEOUT_MS 500         /* retransmission timeout per packet */
#define DUP_ACK_THRESHOLD 3    /* fast retransmit trigger */

/*
 * RUDP packet header, sent on the wire in this exact byte layout:
 *   seq_num      4 bytes
 *   ack_num      4 bytes
 *   flags        1 byte
 *   checksum     2 bytes
 *   payload_len  2 bytes
 *   payload      payload_len bytes
 *
 * We do NOT send this struct directly with sizeof() — struct padding
 * differs across compilers/platforms. Instead we manually serialize
 * each field into a flat byte buffer (see rudp_pack / rudp_unpack).
 */
typedef struct {
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  flags;
    uint16_t checksum;
    uint16_t payload_len;
    uint8_t  payload[MAX_PAYLOAD];
} rudp_packet_t;

/* Fixed size of the header on the wire (excludes payload) */
#define RUDP_HEADER_SIZE 13  /* 4 + 4 + 1 + 2 + 2 */

int rudp_pack(const rudp_packet_t *pkt, uint8_t *buf, size_t buf_size);
int rudp_unpack(const uint8_t *buf, size_t len, rudp_packet_t *pkt);
uint16_t rudp_checksum(const rudp_packet_t *pkt);
void rudp_print_packet(const char *label, const rudp_packet_t *pkt);

#endif /* COMMON_H */
