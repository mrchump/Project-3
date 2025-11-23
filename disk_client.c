// disk_client.c
// Interactive client for the disk server (Part 3)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>

#define BLOCK_SIZE 128

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        return 1;
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    FILE *server = fdopen(sock, "r+");
    if (!server) {
        perror("fdopen");
        close(sock);
        return 1;
    }

    printf("Connected to disk server %s:%d\n", server_ip, port);
    printf("Commands:\n");
    printf("  I                -> get disk geometry\n");
    printf("  R c s            -> read cylinder c, sector s\n");
    printf("  W c s l          -> write l bytes to (c,s), then you type data\n");
    printf("Type Ctrl+D to quit.\n\n");

    char line[1024];

    while (1) {
        printf("> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            break; // EOF
        }

        if (line[0] == 'I') {
            // Send "I\n"
            fputs("I\n", server);
            fflush(server);

            char resp[1024];
            if (fgets(resp, sizeof(resp), server)) {
                printf("Server: %s", resp);
            } else {
                printf("Disconnected.\n");
                break;
            }
        } else if (line[0] == 'R') {
            // Forward line as-is
            fputs(line, server);
            fflush(server);

            int ch = fgetc(server);
            if (ch == EOF) {
                printf("Disconnected.\n");
                break;
            }
            if (ch == '0') {
                printf("Read failed.\n");
            } else if (ch == '1') {
                unsigned char buf[BLOCK_SIZE];
                int srv_fd = fileno(server);
                ssize_t n = read(srv_fd, buf, BLOCK_SIZE);
                if (n != BLOCK_SIZE) {
                    printf("Short read from server.\n");
                    break;
                }
                printf("Data (printable / '.' for others):\n");
                for (int i = 0; i < BLOCK_SIZE; i++) {
                    unsigned char c = buf[i];
                    putchar(isprint(c) ? c : '.');
                }
                putchar('\n');
            } else {
                printf("Unexpected response: %c\n", ch);
            }
        } else if (line[0] == 'W') {
            int c, s, l;
            if (sscanf(line, "W %d %d %d", &c, &s, &l) != 3) {
                printf("Usage: W c s l\n");
                continue;
            }
            if (l < 0 || l > BLOCK_SIZE) {
                printf("l must be between 0 and %d\n", BLOCK_SIZE);
                continue;
            }

            // Send header line first
            fputs(line, server);
            fflush(server);

            // Ask user for data
            unsigned char buf[BLOCK_SIZE];
            memset(buf, 0, sizeof(buf));

            printf("Enter %d bytes of data (end with newline, extra ignored):\n", l);
            fflush(stdout);

            // Read from stdin up to l bytes
            int total = 0;
            while (total < l) {
                int ch2 = getchar();
                if (ch2 == EOF || ch2 == '\n') break;
                buf[total++] = (unsigned char)ch2;
            }

            // If user typed fewer than l bytes before newline, pad rest with zeros
            int server_fd = fileno(server);
            ssize_t w = write(server_fd, buf, l);
            if (w != l) {
                perror("write data to server");
                break;
            }

            int resp = fgetc(server);
            if (resp == EOF) {
                printf("Disconnected.\n");
                break;
            } else if (resp == '1') {
                printf("Write OK.\n");
            } else {
                printf("Write failed.\n");
            }
        } else {
            printf("Unknown command. Use I, R, or W.\n");
        }
    }

    fclose(server);
    return 0;
}
