// Basic_disk_storage_system.c
// Disk server for Project 3 – Part 3

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#define BLOCK_SIZE 128
#define BACKLOG 10

static int num_cylinders;
static int sectors_per_cylinder;
static int seek_usec;
static int disk_fd;

static off_t get_offset(int c, int s) {
    return ((off_t)c * sectors_per_cylinder + s) * BLOCK_SIZE;
}

static int valid_block(int c, int s) {
    if (c < 0 || c >= num_cylinders) return 0;
    if (s < 0 || s >= sectors_per_cylinder) return 0;
    return 1;
}

static void simulate_seek(int new_cylinder, int *last_cylinder) {
    int diff = abs(new_cylinder - *last_cylinder);
    useconds_t sleep_time = (useconds_t)diff * (useconds_t)seek_usec;
    if (sleep_time > 0) {
        usleep(sleep_time);
    }
    *last_cylinder = new_cylinder;
}

static void handle_read(FILE *client, int *last_cylinder, int c, int s) {
    if (!valid_block(c, s)) {
        fputc('0', client);
        fflush(client);
        return;
    }

    simulate_seek(c, last_cylinder);

    off_t offset = get_offset(c, s);
    if (lseek(disk_fd, offset, SEEK_SET) < 0) {
        perror("lseek (read)");
        fputc('0', client);
        fflush(client);
        return;
    }

    unsigned char buf[BLOCK_SIZE];
    ssize_t n = read(disk_fd, buf, BLOCK_SIZE);
    if (n != BLOCK_SIZE) {
        perror("read (disk)");
        fputc('0', client);
        fflush(client);
        return;
    }

    // success: send '1' then 128 bytes
    fputc('1', client);
    fflush(client);

    int client_fd = fileno(client);
    ssize_t w = write(client_fd, buf, BLOCK_SIZE);
    if (w != BLOCK_SIZE) {
        perror("write (to client)");
    }
}

static void handle_write(FILE *client, int *last_cylinder, char *line) {
    int c, s, l;

    // Parse "W c s l"
    if (sscanf(line, "W %d %d %d", &c, &s, &l) != 3) {
        fputc('0', client);
        fflush(client);
        return;
    }

    if (!valid_block(c, s) || l < 0 || l > BLOCK_SIZE) {
        fputc('0', client);
        fflush(client);
        return;
    }

    int client_fd = fileno(client);
    unsigned char buf[BLOCK_SIZE];
    memset(buf, 0, sizeof(buf));

    // Read exactly l bytes from client as data payload
    ssize_t total = 0;
    while (total < l) {
        ssize_t r = read(client_fd, buf + total, l - total);
        if (r <= 0) {
            perror("read (write data from client)");
            fputc('0', client);
            fflush(client);
            return;
        }
        total += r;
    }

    simulate_seek(c, last_cylinder);

    off_t offset = get_offset(c, s);
    if (lseek(disk_fd, offset, SEEK_SET) < 0) {
        perror("lseek (write)");
        fputc('0', client);
        fflush(client);
        return;
    }

    // Write full 128-byte block (zero-filled if l < 128)
    ssize_t w = write(disk_fd, buf, BLOCK_SIZE);
    if (w != BLOCK_SIZE) {
        perror("write (disk)");
        fputc('0', client);
        fflush(client);
        return;
    }

    fputc('1', client);
    fflush(client);
}

static void handle_client(int client_sock) {
    FILE *client = fdopen(client_sock, "r+");
    if (!client) {
        perror("fdopen");
        close(client_sock);
        return;
    }

    char line[1024];
    int last_cylinder = 0;

    while (fgets(line, sizeof(line), client) != NULL) {
        if (line[0] == 'I') {
            // Information request
            fprintf(client, "%d %d\n", num_cylinders, sectors_per_cylinder);
            fflush(client);
        } else if (line[0] == 'R') {
            int c, s;
            if (sscanf(line, "R %d %d", &c, &s) != 2) {
                fputc('0', client);
                fflush(client);
                continue;
            }
            handle_read(client, &last_cylinder, c, s);
        } else if (line[0] == 'W') {
            handle_write(client, &last_cylinder, line);
        } else {
            // Unknown command – ignore or send failure
            fputc('0', client);
            fflush(client);
        }
    }

    fclose(client); // also closes client_sock
}

int main(int argc, char *argv[]) {
    if (argc != 6) {
        fprintf(stderr,
                "Usage: %s <port> <num_cyl> <sec_per_cyl> <seek_usec> <disk_file>\n",
                argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    num_cylinders = atoi(argv[2]);
    sectors_per_cylinder = atoi(argv[3]);
    seek_usec = atoi(argv[4]);
    const char *disk_file = argv[5];

    if (num_cylinders <= 0 || sectors_per_cylinder <= 0 || seek_usec < 0) {
        fprintf(stderr, "Invalid geometry or seek time.\n");
        return 1;
    }

    off_t total_blocks = (off_t)num_cylinders * sectors_per_cylinder;
    off_t file_size = total_blocks * BLOCK_SIZE;

    // Open or create disk file and size it with ftruncate
    disk_fd = open(disk_file, O_RDWR | O_CREAT, 0666);
    if (disk_fd < 0) {
        perror("open disk_file");
        return 1;
    }

    if (ftruncate(disk_fd, file_size) < 0) {
        perror("ftruncate disk_file");
        close(disk_fd);
        return 1;
    }

    // Set up listening socket
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        close(disk_fd);
        return 1;
    }

    int optval = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        close(disk_fd);
        return 1;
    }

    if (listen(listen_fd, BACKLOG) < 0) {
        perror("listen");
        close(listen_fd);
        close(disk_fd);
        return 1;
    }

    printf("Disk server listening on port %d\n", port);
    printf("Geometry: %d cylinders, %d sectors/cylinder, block size %d\n",
           num_cylinders, sectors_per_cylinder, BLOCK_SIZE);

    while (1) {
        int client_sock = accept(listen_fd, NULL, NULL);
        if (client_sock < 0) {
            perror("accept");
            continue;
        }

        // Single-threaded for simplicity – handle one client at a time
        handle_client(client_sock);
    }

    close(listen_fd);
    close(disk_fd);
    return 0;
}
