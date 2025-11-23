// fs_client.c
// Command-line client for File_system_server

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>

static ssize_t read_exact(int fd, unsigned char *buf, int len) {
    int total = 0;
    while (total < len) {
        ssize_t n = read(fd, buf + total, len - total);
        if (n <= 0) return n;
        total += n;
    }
    return total;
}

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

    printf("Connected to filesystem server %s:%d\n", server_ip, port);
    printf("Commands:\n");
    printf("  F                   - format filesystem\n");
    printf("  C name              - create file\n");
    printf("  D name              - delete file\n");
    printf("  L 0|1               - list files\n");
    printf("  R name              - read file\n");
    printf("  W name len          - write len bytes (you will be prompted for data)\n");
    printf("Ctrl+D to quit.\n\n");

    char line[1024];

    while (1) {
        printf("fs> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin))
            break; // EOF

        if (line[0] == '\n' || line[0] == '\0')
            continue;

        if (line[0] == 'W') {
            // Parse: W name len
            char fname[64];
            int len;
            if (sscanf(line, "W %63s %d", fname, &len) != 2 || len < 0) {
                printf("Usage: W name len\n");
                continue;
            }

            // Send header line
            fprintf(server, "W %s %d\n", fname, len);
            fflush(server);

            unsigned char *buf = NULL;
            if (len > 0) {
                buf = (unsigned char *)malloc(len);
                if (!buf) {
                    printf("Memory error\n");
                    return 1;
                }
                printf("Enter %d bytes of data (end with newline, extra ignored):\n",
                       len);
                fflush(stdout);

                int total = 0;
                while (total < len) {
                    int ch = getchar();
                    if (ch == EOF || ch == '\n')
                        break;
                    buf[total++] = (unsigned char)ch;
                }
                // If user typed fewer chars than len, pad with zeros
                while (total < len) {
                    buf[total++] = 0;
                }

                int srv_fd = fileno(server);
                if (write(srv_fd, buf, len) != len) {
                    perror("write data");
                    free(buf);
                    break;
                }
                free(buf);
            }

            // Read return code line
            char resp[128];
            if (!fgets(resp, sizeof(resp), server)) {
                printf("Server closed connection.\n");
                break;
            }
            printf("Result: %s", resp);
        } else if (line[0] == 'R') {
            // Send read command as-is
            fputs(line, server);
            if (line[strlen(line) - 1] != '\n')
                fputc('\n', server);
            fflush(server);

            // Response: rc length data\n
            int srv_fd = fileno(server);
            char header[128];
            int pos = 0;
            // read until first space after length
            while (pos < (int)sizeof(header) - 1) {
                char c;
                ssize_t n = read(srv_fd, &c, 1);
                if (n <= 0) {
                    printf("Server closed.\n");
                    goto done;
                }
                header[pos++] = c;
                if (c == ' ') break; // after length
            }
            header[pos] = '\0';

            int rc, len;
            // header contains "rc len "
            if (sscanf(header, "%d %d", &rc, &len) != 2) {
                printf("Bad response\n");
                continue;
            }

            printf("Return code: %d, length: %d\n", rc, len);
            if (rc != 0 || len <= 0) {
                // Read until newline to clear buffer
                char ch;
                while (read(srv_fd, &ch, 1) == 1 && ch != '\n') {}
                continue;
            }

            unsigned char *buf = (unsigned char *)malloc(len + 1);
            if (!buf) {
                printf("Memory error\n");
                goto done;
            }
            if (read_exact(srv_fd, buf, len) != len) {
                printf("Short read\n");
                free(buf);
                goto done;
            }
            buf[len] = '\0';

            // Discard trailing newline after data
            char ch;
            while (read(srv_fd, &ch, 1) == 1 && ch != '\n') {}

            printf("Data: ");
            for (int i = 0; i < len; i++) {
                unsigned char c = buf[i];
                putchar(isprint(c) ? c : '.');
            }
            putchar('\n');
            free(buf);
        } else if (line[0] == 'L') {
            // Send L command
            fputs(line, server);
            if (line[strlen(line) - 1] != '\n')
                fputc('\n', server);
            fflush(server);

            // First line is status code
            char resp[256];
            if (!fgets(resp, sizeof(resp), server)) {
                printf("Server closed.\n");
                break;
            }
            printf("Status: %s", resp);

            // Then listing lines until "END\n"
            while (1) {
                if (!fgets(resp, sizeof(resp), server)) {
                    printf("Server closed.\n");
                    goto done;
                }
                if (strcmp(resp, "END\n") == 0)
                    break;
                printf("%s", resp);
            }
        } else if (line[0] == 'F' || line[0] == 'C' || line[0] == 'D') {
            // Simple one-line commands: send, then read one-line result
            fputs(line, server);
            if (line[strlen(line) - 1] != '\n')
                fputc('\n', server);
            fflush(server);

            char resp[128];
            if (!fgets(resp, sizeof(resp), server)) {
                printf("Server closed.\n");
                break;
            }
            printf("Result: %s", resp);
        } else {
            printf("Unknown command. Use F, C, D, L, R, or W.\n");
        }
    }

done:
    fclose(server);
    return 0;
}

