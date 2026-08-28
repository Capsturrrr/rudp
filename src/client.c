#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "common.h"

int main(void) {
    int sockfd;
    struct sockaddr_in server_addr;
    socklen_t server_len = sizeof(server_addr);
    char buffer[BUFFER_SIZE];

    /* 1. Create a UDP socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    /* 2. Describe where the server is */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("invalid server IP");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("RUDP echo client. Type a message and press Enter (type 'quit' to exit).\n");

    char input[BUFFER_SIZE];
    while (1) {
        printf("> ");
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
        /* strip trailing newline */
        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "quit") == 0) {
            break;
        }

        /* 3. Send the message to the server */
        ssize_t sent = sendto(sockfd, input, strlen(input), 0,
                               (const struct sockaddr *)&server_addr, server_len);
        if (sent < 0) {
            perror("sendto failed");
            continue;
        }

        /* 4. Wait for the echoed reply */
        ssize_t n = recvfrom(sockfd, buffer, BUFFER_SIZE - 1, 0, NULL, NULL);
        if (n < 0) {
            perror("recvfrom failed");
            continue;
        }
        buffer[n] = '\0';

        printf("Echo from server: \"%s\"\n", buffer);
    }

    close(sockfd);
    printf("Client closed.\n");
    return 0;
}
