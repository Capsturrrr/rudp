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
    struct sockaddr_in server_addr;
    socklen_t server_len = sizeof(server_addr);
    uint8_t recv_buf[BUFFER_SIZE];

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    /* Timeout on recvfrom so we can retry the handshake if a SYN-ACK is lost */
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

    /* --- Step 1: send SYN, retry on timeout --- */
    rudp_packet_t syn;
    memset(&syn, 0, sizeof(syn));
    syn.seq_num = client_seq;
    syn.flags = FLAG_SYN;
    syn.payload_len = 0;

    int attempt;
    int handshake_ok = 0;
    for (attempt = 1; attempt <= MAX_HANDSHAKE_RETRIES; attempt++) {
        printf("Sending SYN (attempt %d)...\n", attempt);
        send_packet(sockfd, &syn, &server_addr, server_len);

        rudp_packet_t reply;
        ssize_t n = recvfrom(sockfd, recv_buf, sizeof(recv_buf), 0, NULL, NULL);
        if (n < 0) {
            printf("Timed out waiting for SYN-ACK, retrying...\n");
            continue;
        }

        if (rudp_unpack(recv_buf, (size_t)n, &reply) != 0) {
            printf("Malformed reply, retrying...\n");
            continue;
        }

        if ((reply.flags & FLAG_SYN) && (reply.flags & FLAG_ACK) &&
            reply.ack_num == client_seq + 1) {
            server_seq = reply.seq_num;
            rudp_print_packet("CLIENT RECV", &reply);

            /* --- Step 3: send final ACK --- */
            rudp_packet_t ack;
            memset(&ack, 0, sizeof(ack));
            ack.seq_num = client_seq + 1;
            ack.ack_num = server_seq + 1;
            ack.flags = FLAG_ACK;
            send_packet(sockfd, &ack, &server_addr, server_len);
            rudp_print_packet("CLIENT SEND", &ack);

            handshake_ok = 1;
            break;
        }
    }

    if (!handshake_ok) {
        fprintf(stderr, "Handshake failed after %d attempts.\n", MAX_HANDSHAKE_RETRIES);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Handshake complete. Connection established.\n\n");

    /* --- Interactive DATA loop --- */
    uint32_t seq = client_seq + 1;
    char input[MAX_PAYLOAD];
    printf("Type a message and press Enter (type 'quit' to close connection).\n");

    while (1) {
        printf("> ");
        if (fgets(input, sizeof(input), stdin) == NULL) break;
        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "quit") == 0) {
            rudp_packet_t fin;
            memset(&fin, 0, sizeof(fin));
            fin.seq_num = seq;
            fin.flags = FLAG_FIN;
            send_packet(sockfd, &fin, &server_addr, server_len);

            rudp_packet_t ack;
            recvfrom(sockfd, recv_buf, sizeof(recv_buf), 0, NULL, NULL);
            rudp_unpack(recv_buf, sizeof(recv_buf), &ack); /* best-effort */
            printf("Connection closed.\n");
            break;
        }

        rudp_packet_t data;
        memset(&data, 0, sizeof(data));
        data.seq_num = seq;
        data.flags = FLAG_DATA;
        data.payload_len = (uint16_t)strlen(input);
        memcpy(data.payload, input, data.payload_len);

        send_packet(sockfd, &data, &server_addr, server_len);

        rudp_packet_t ack;
        ssize_t n = recvfrom(sockfd, recv_buf, sizeof(recv_buf), 0, NULL, NULL);
        if (n > 0 && rudp_unpack(recv_buf, (size_t)n, &ack) == 0) {
            printf("Server ACKed seq=%u\n", ack.ack_num - 1);
        }

        seq++;
    }

    close(sockfd);
    return 0;
}
