// random_client.c
// Random workload generator for disk server (Part 3)
// Usage: ./random_client <server_ip> <port> <N> <seed>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define BLOCK_SIZE 128

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <server_ip> <port> <N> <seed>\n", argv[0]);
        return 1;
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    int N = atoi(argv[3]);
    int seed = atoi(argv[4]);

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

    // Get disk geometry using I command
    fputs("I\n", server);
    fflush(server);

    int num_cyl, sectors_per_cyl;
    if (fscanf(server, "%d %d", &num_cyl, &sectors_per_cyl) != 2) {
        fprintf(stderr, "Failed to read disk geometry\n");
        fclose(server);
        return 1;
    }

    // Consume the rest of the line after fscanf
    int ch;
    while ((ch = fgetc(server)) != '\n' && ch != EOF) {}

    printf("Disk geometry: %d cylinders, %d sectors/cylinder\n",
           num_cyl, sectors_per_cyl);

    srand(seed);

    unsigned char buf[BLOCK_SIZE];

    for (int i = 0; i < N; i++) {
        int is_write = rand() % 2;
        int c = rand() % num_cyl;
        int s = rand() % sectors_per_cyl;

        if (is_write) {
            // Build random 128-byte payload
            for (int j = 0; j < BLOCK_SIZE; j++) {
                buf[j] = (unsigned char)('A' + (rand() % 26));
            }

            // Send header: W c s 128\n
            char header[128];
            snprintf(header, sizeof(header), "W %d %d %d\n", c, s, BLOCK_SIZE);
            fputs(header, server);
            fflush(server);

            int srv_fd = fileno(server);
            ssize_t w = write(srv_fd, buf, BLOCK_SIZE);
            if (w != BLOCK_SIZE) {
                perror("write payload");
                break;
            }

            int resp = fgetc(server);
            if (resp == EOF) {
                printf("\nServer disconnected.\n");
                break;
            }
            // Just show progress
            putchar('W');
            fflush(stdout);
        } else {
            // Read request: R c s\n
            char header[128];
            snprintf(header, sizeof(header), "R %d %d\n", c, s);
            fputs(header, server);
            fflush(server);

            int resp = fgetc(server);
            if (resp == EOF) {
                printf("\nServer disconnected.\n");
                break;
            }
            if (resp == '1') {
                int srv_fd = fileno(server);
                ssize_t n = read(srv_fd, buf, BLOCK_SIZE);
                if (n != BLOCK_SIZE) {
                    perror("read block");
                    break;
                }
            }
            // Just show progress
            putchar('R');
            fflush(stdout);
        }
    }

    putchar('\n');
    fclose(server);
    return 0;
}
