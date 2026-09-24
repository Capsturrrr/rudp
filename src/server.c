#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "common.h"

/*
 * Phase 4 server: same handshake as Phase 3, but now the DATA phase
 * implements a Go-Back-N receiver:
 *
 *   - Tracks `expected_seq`, the next in-order sequence number it wants.
 *   - If an incoming DATA packet's seq == expected_seq: accept it,
 *     deliver the payload, bump expected_seq, send a cumulative ACK
 *     (ack_num = expected_seq).
 *   - If seq != expected_seq (out of order, or a duplicate of something
 *     already delivered): DISCARD the payload and re-send an ACK for
 *     expected_seq. This is what drives the sender's duplicate-ACK
 *     counter and triggers fast retransmit.
 *
 * Go-Back-N deliberately does not buffer out-of-order packets — it's
 * simpler to reason about and implement correctly than Selective Repeat,
 * and it's the classic textbook baseline this project is meant to explore.
 */

typedef enum {
    STATE_LISTEN,
    STATE_SYN_RECEIVED,
    STATE_ESTABLISHED,
} conn_state_t;

static const char *state_name(conn_state_t s) {
    switch (s) {
        case STATE_LISTEN:       return "LISTEN";
        case STATE_SYN_RECEIVED: return "SYN_RECEIVED";
        case STATE_ESTABLISHED:  return "ESTABLISHED";
        default:                 return "UNKNOWN";
    }
}

static void send_packet(int sockfd, const rudp_packet_t *pkt,
                         const struct sockaddr_in *addr, socklen_t addr_len) {
    uint8_t buf[BUFFER_SIZE];
    int len = rudp_pack(pkt, buf, sizeof(buf));
    if (len < 0) {
        fprintf(stderr, "rudp_pack failed\n");
        return;
    }
    if (sendto(sockfd, buf, (size_t)len, 0,
               (const struct sockaddr *)addr, addr_len) < 0) {
        perror("sendto failed");
    }
}

int main(void) {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    uint8_t recv_buf[BUFFER_SIZE];

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("RUDP server listening on port %d...\n", SERVER_PORT);

    conn_state_t state = STATE_LISTEN;
    uint32_t server_seq = 0;
    uint32_t expected_seq = 0;   /* Go-Back-N: next in-order seq we want from client */
    char reassembled[65536];
    size_t reassembled_len = 0;

    srand((unsigned int)time(NULL));

    while (1) {
        ssize_t n = recvfrom(sockfd, recv_buf, sizeof(recv_buf), 0,
                              (struct sockaddr *)&client_addr, &client_len);
        if (n < 0) {
            perror("recvfrom failed");
            continue;
        }

        rudp_packet_t pkt;
        int result = rudp_unpack(recv_buf, (size_t)n, &pkt);
        if (result == -2) {
            printf("Dropped corrupted packet (checksum mismatch)\n");
            continue;
        } else if (result != 0) {
            printf("Dropped malformed packet\n");
            continue;
        }

        if (pkt.flags & FLAG_SYN) {
            /* Accept a fresh SYN in ANY state, not just LISTEN.
             * This makes the server resilient to a prior connection being
             * killed mid-session (Ctrl+C, timeout, crash) — without this,
             * a stuck non-LISTEN state would silently reject every future
             * client forever. Real servers must tolerate this. */
            uint32_t client_seq = pkt.seq_num;
            server_seq = (uint32_t)rand();
            expected_seq = client_seq + 1; /* first DATA packet will carry this seq */
            reassembled_len = 0;

            rudp_packet_t reply;
            memset(&reply, 0, sizeof(reply));
            reply.seq_num = server_seq;
            reply.ack_num = client_seq + 1;
            reply.flags = FLAG_SYN | FLAG_ACK;
            send_packet(sockfd, &reply, &client_addr, client_len);

            state = STATE_SYN_RECEIVED;
            printf("Handshake: SYN received. State: %s\n", state_name(state));

        } else if ((pkt.flags & FLAG_ACK) && state == STATE_SYN_RECEIVED &&
                   pkt.ack_num == server_seq + 1) {
            state = STATE_ESTABLISHED;
            printf("Handshake complete. State: %s (expecting seq=%u)\n\n",
                   state_name(state), expected_seq);

        } else if ((pkt.flags & FLAG_DATA) && state == STATE_ESTABLISHED) {
            if (pkt.seq_num == expected_seq) {
                /* In-order — accept and deliver */
                if (reassembled_len + pkt.payload_len < sizeof(reassembled)) {
                    memcpy(reassembled + reassembled_len, pkt.payload, pkt.payload_len);
                    reassembled_len += pkt.payload_len;
                }
                printf("ACCEPTED seq=%u (%u bytes): \"%.*s\"\n",
                       pkt.seq_num, pkt.payload_len, pkt.payload_len, pkt.payload);
                expected_seq++;

                rudp_packet_t ack;
                memset(&ack, 0, sizeof(ack));
                ack.seq_num = server_seq;
                ack.ack_num = expected_seq;
                ack.flags = FLAG_ACK;
                send_packet(sockfd, &ack, &client_addr, client_len);
                printf("  -> cumulative ACK %u\n\n", expected_seq);

            } else {
                /* Out of order or duplicate — discard, re-ACK what we actually want */
                printf("DISCARDED seq=%u (expected %u) — out of order/duplicate\n",
                       pkt.seq_num, expected_seq);

                rudp_packet_t ack;
                memset(&ack, 0, sizeof(ack));
                ack.seq_num = server_seq;
                ack.ack_num = expected_seq;
                ack.flags = FLAG_ACK;
                send_packet(sockfd, &ack, &client_addr, client_len);
                printf("  -> duplicate ACK %u\n\n", expected_seq);
            }

        } else if ((pkt.flags & FLAG_FIN) && state == STATE_ESTABLISHED) {
            rudp_packet_t ack;
            memset(&ack, 0, sizeof(ack));
            ack.seq_num = server_seq;
            ack.ack_num = pkt.seq_num + 1;
            ack.flags = FLAG_ACK;
            send_packet(sockfd, &ack, &client_addr, client_len);

            printf("Connection closed. Full message reassembled (%zu bytes):\n\"%.*s\"\n\n",
                   reassembled_len, (int)reassembled_len, reassembled);

            state = STATE_LISTEN;
            printf("State: %s (ready for new connection)\n\n", state_name(state));

        } else {
            printf("Unexpected packet (flags=0x%02X) in state %s — ignored\n\n",
                   pkt.flags, state_name(state));
        }
    }

    close(sockfd);
    return 0;
}
