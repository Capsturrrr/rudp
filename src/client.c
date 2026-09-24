#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "common.h"

#define HANDSHAKE_TIMEOUT_SEC 3
#define MAX_HANDSHAKE_RETRIES 5
#define MAX_CHUNKS 64

/*
 * Phase 4 client: after the handshake, sends a message split into
 * fixed-size chunks using a Go-Back-N sliding window:
 *
 *   - Up to WINDOW_SIZE packets are outstanding (sent, unacked) at once.
 *   - `base` = seq of the oldest unacked packet (the window's left edge).
 *   - `next_seq` = seq of the next packet not yet sent.
 *   - On receiving a cumulative ACK that advances past `base`, the
 *     window slides forward and the retransmission timer resets.
 *   - On receiving a duplicate ACK (same ack_num as last time) 3 times
 *     in a row, we fast-retransmit `base` immediately (no timeout wait).
 *   - On timeout with no new ACK, we retransmit the ENTIRE window from
 *     `base` onward (classic Go-Back-N: resend everything in flight).
 *
 * `loss_percent` (optional CLI arg) randomly "drops" outgoing DATA
 * packets to let you test retransmission behavior without needing
 * tc/netem yet — e.g. `./client 20` simulates 20% packet loss.
 */

static int loss_percent = 0;

static int should_drop(void) {
    if (loss_percent <= 0) return 0;
    return (rand() % 100) < loss_percent;
}

static void send_packet(int sockfd, const rudp_packet_t *pkt,
                         const struct sockaddr_in *addr, socklen_t addr_len,
                         int allow_drop) {
    uint8_t buf[BUFFER_SIZE];
    int len = rudp_pack(pkt, buf, sizeof(buf));
    if (len < 0) {
        fprintf(stderr, "rudp_pack failed\n");
        return;
    }

    if (allow_drop && should_drop()) {
        printf("  [SIMULATED LOSS] dropped seq=%u\n", pkt->seq_num);
        return;
    }

    if (sendto(sockfd, buf, (size_t)len, 0,
               (const struct sockaddr *)addr, addr_len) < 0) {
        perror("sendto failed");
    }
}

static long now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int main(int argc, char *argv[]) {
    if (argc > 1) {
        loss_percent = atoi(argv[1]);
        printf("Simulated packet loss: %d%%\n", loss_percent);
    }

    int sockfd;
    struct sockaddr_in server_addr;
    socklen_t server_len = sizeof(server_addr);
    uint8_t recv_buf[BUFFER_SIZE];

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct timeval tv;
    tv.tv_sec = HANDSHAKE_TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    srand((unsigned int)time(NULL));
    uint32_t client_seq = (uint32_t)rand();
    uint32_t server_seq = 0;

    /* --- Handshake (unchanged from Phase 3) --- */
    rudp_packet_t syn;
    memset(&syn, 0, sizeof(syn));
    syn.seq_num = client_seq;
    syn.flags = FLAG_SYN;

    int handshake_ok = 0;
    for (int attempt = 1; attempt <= MAX_HANDSHAKE_RETRIES; attempt++) {
        printf("Sending SYN (attempt %d)...\n", attempt);
        send_packet(sockfd, &syn, &server_addr, server_len, 0);

        rudp_packet_t reply;
        ssize_t n = recvfrom(sockfd, recv_buf, sizeof(recv_buf), 0, NULL, NULL);
        if (n < 0) {
            printf("Timed out waiting for SYN-ACK, retrying...\n");
            continue;
        }
        if (rudp_unpack(recv_buf, (size_t)n, &reply) != 0) continue;

        if ((reply.flags & FLAG_SYN) && (reply.flags & FLAG_ACK) &&
            reply.ack_num == client_seq + 1) {
            server_seq = reply.seq_num;

            rudp_packet_t ack;
            memset(&ack, 0, sizeof(ack));
            ack.seq_num = client_seq + 1;
            ack.ack_num = server_seq + 1;
            ack.flags = FLAG_ACK;
            send_packet(sockfd, &ack, &server_addr, server_len, 0);

            handshake_ok = 1;
            break;
        }
    }

    if (!handshake_ok) {
        fprintf(stderr, "Handshake failed.\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Handshake complete. Connection established.\n\n");

    /* --- Build the message to send, split into fixed-size chunks --- */
    const char *message =
        "This is a longer test message for RUDP Phase 4. It is deliberately "
        "split across several packets so the sliding window, retransmission "
        "timeout, and fast retransmit logic all get exercised during a "
        "single transfer instead of one line at a time.";

    size_t msg_len = strlen(message);
    size_t chunk_size = 40; /* small on purpose, to force multiple packets */
    int total_packets = (int)((msg_len + chunk_size - 1) / chunk_size);
    if (total_packets > MAX_CHUNKS) total_packets = MAX_CHUNKS;

    uint32_t base_seq = client_seq + 1; /* first DATA packet's seq */
    rudp_packet_t packets[MAX_CHUNKS];

    for (int i = 0; i < total_packets; i++) {
        memset(&packets[i], 0, sizeof(rudp_packet_t));
        size_t offset = (size_t)i * chunk_size;
        size_t len = (offset + chunk_size <= msg_len) ? chunk_size : (msg_len - offset);
        packets[i].seq_num = base_seq + (uint32_t)i;
        packets[i].flags = FLAG_DATA;
        packets[i].payload_len = (uint16_t)len;
        memcpy(packets[i].payload, message + offset, len);
    }

    printf("Sending %d packets (message split into %zu-byte chunks)...\n\n",
           total_packets, chunk_size);

    /* --- Sliding window send loop (Go-Back-N) --- */
    int base = 0;        /* index into packets[] of oldest unacked packet */
    int next_seq = 0;     /* index of next packet not yet sent */
    long timer_start = 0;
    int timer_active = 0;
    uint32_t last_ack_seen = 0;
    int dup_ack_count = 0;

    tv.tv_sec = 0;
    tv.tv_usec = 50000; /* 50ms poll interval for recvfrom while managing our own timer */
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (base < total_packets) {
        /* Send everything the window currently allows */
        while (next_seq < total_packets && next_seq < base + WINDOW_SIZE) {
            printf("SEND seq=%u (window: base=%d next=%d)\n",
                   packets[next_seq].seq_num, base, next_seq);
            send_packet(sockfd, &packets[next_seq], &server_addr, server_len, 1);
            if (!timer_active) {
                timer_start = now_ms();
                timer_active = 1;
            }
            next_seq++;
        }

        rudp_packet_t ack;
        ssize_t n = recvfrom(sockfd, recv_buf, sizeof(recv_buf), 0, NULL, NULL);

        if (n > 0 && rudp_unpack(recv_buf, (size_t)n, &ack) == 0 && (ack.flags & FLAG_ACK)) {
            uint32_t acked_seq = ack.ack_num; /* "I have everything before this" */
            uint32_t base_pkt_seq = packets[base].seq_num;

            if (acked_seq > base_pkt_seq) {
                /* Window slides forward */
                while (base < total_packets && packets[base].seq_num < acked_seq) {
                    base++;
                }
                printf("  <- ACK %u — window slides to base=%d\n\n", acked_seq, base);
                dup_ack_count = 0;
                last_ack_seen = acked_seq;
                timer_active = (base < total_packets);
                if (timer_active) timer_start = now_ms();

            } else if (acked_seq == last_ack_seen && acked_seq == base_pkt_seq) {
                /* Duplicate ACK — receiver still wants `base`, something after it was lost */
                dup_ack_count++;
                printf("  <- duplicate ACK %u (count=%d)\n", acked_seq, dup_ack_count);

                if (dup_ack_count >= DUP_ACK_THRESHOLD) {
                    printf("  !! FAST RETRANSMIT seq=%u\n\n", packets[base].seq_num);
                    send_packet(sockfd, &packets[base], &server_addr, server_len, 1);
                    dup_ack_count = 0;
                    timer_start = now_ms();
                }
            } else {
                last_ack_seen = acked_seq;
            }

        } else {
            /* No packet arrived this poll interval — check our own RTO timer */
            if (timer_active && (now_ms() - timer_start) >= TIMEOUT_MS) {
                printf("  !! TIMEOUT — retransmitting window from base=%d to next=%d\n\n",
                       base, next_seq - 1);
                for (int i = base; i < next_seq; i++) {
                    send_packet(sockfd, &packets[i], &server_addr, server_len, 1);
                }
                timer_start = now_ms();
                dup_ack_count = 0;
            }
        }
    }

    printf("All %d packets delivered and acknowledged.\n\n", total_packets);

    /* --- Teardown --- */
    rudp_packet_t fin;
    memset(&fin, 0, sizeof(fin));
    fin.seq_num = base_seq + (uint32_t)total_packets;
    fin.flags = FLAG_FIN;
    send_packet(sockfd, &fin, &server_addr, server_len, 0);

    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    recvfrom(sockfd, recv_buf, sizeof(recv_buf), 0, NULL, NULL); /* best-effort final ACK */

    printf("Connection closed.\n");
    close(sockfd);
    return 0;
}
