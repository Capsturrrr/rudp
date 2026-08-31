#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "common.h"

/*
 * Phase 3: the server now performs a three-way handshake before
 * accepting any DATA packets:
 *
 *   Client --- SYN (seq=X) --------------> Server
 *   Client <-- SYN+ACK (seq=Y, ack=X+1) -- Server
 *   Client --- ACK (ack=Y+1) ------------> Server
 *
 * After the handshake, the server enters a simple echo loop for DATA
 * packets, and tears down the connection on a FIN.
 */

typedef enum {
    STATE_LISTEN,
    STATE_SYN_RECEIVED,
    STATE_ESTABLISHED,
    STATE_CLOSED
} conn_state_t;

static const char *state_name(conn_state_t s) {
    switch (s) {
        case STATE_LISTEN:       return "LISTEN";
        case STATE_SYN_RECEIVED: return "SYN_RECEIVED";
        case STATE_ESTABLISHED:  return "ESTABLISHED";
        case STATE_CLOSED:       return "CLOSED";
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
    uint32_t server_seq = 0;   /* our own sequence number, chosen at SYN-ACK time */
    uint32_t client_seq = 0;   /* last seq we've seen from the client */

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

        rudp_print_packet("SERVER RECV", &pkt);

        if ((pkt.flags & FLAG_SYN) && state == STATE_LISTEN) {
            /* Step 1: client wants to connect. Remember its seq, pick our own. */
            client_seq = pkt.seq_num;
            server_seq = (uint32_t)rand();

            rudp_packet_t reply;
            memset(&reply, 0, sizeof(reply));
            reply.seq_num = server_seq;
            reply.ack_num = client_seq + 1;
            reply.flags = FLAG_SYN | FLAG_ACK;
            reply.payload_len = 0;

            send_packet(sockfd, &reply, &client_addr, client_len);
            rudp_print_packet("SERVER SEND", &reply);

            state = STATE_SYN_RECEIVED;
            printf("State: %s\n\n", state_name(state));

        } else if ((pkt.flags & FLAG_ACK) && state == STATE_SYN_RECEIVED &&
                   pkt.ack_num == server_seq + 1) {
            /* Step 3: client acknowledged our SYN-ACK. Connection established. */
            state = STATE_ESTABLISHED;
            printf("Handshake complete. State: %s\n\n", state_name(state));

        } else if ((pkt.flags & FLAG_DATA) && state == STATE_ESTABLISHED) {
            /* Simple echo of DATA packets while connected (reliability comes in Phase 4) */
            printf("DATA received: \"%.*s\"\n", pkt.payload_len, pkt.payload);

            rudp_packet_t ack;
            memset(&ack, 0, sizeof(ack));
            ack.seq_num = server_seq;
            ack.ack_num = pkt.seq_num + 1;
            ack.flags = FLAG_ACK;
            ack.payload_len = 0;
            send_packet(sockfd, &ack, &client_addr, client_len);
            rudp_print_packet("SERVER SEND", &ack);
            printf("\n");

        } else if ((pkt.flags & FLAG_FIN) && state == STATE_ESTABLISHED) {
            /* Teardown */
            rudp_packet_t ack;
            memset(&ack, 0, sizeof(ack));
            ack.seq_num = server_seq;
            ack.ack_num = pkt.seq_num + 1;
            ack.flags = FLAG_ACK;
            send_packet(sockfd, &ack, &client_addr, client_len);

            state = STATE_LISTEN; /* ready for a new connection */
            printf("Connection closed. State: %s\n\n", state_name(state));

        } else {
            printf("Unexpected packet in state %s — ignored\n\n", state_name(state));
        }
    }

    close(sockfd);
    return 0;
}
