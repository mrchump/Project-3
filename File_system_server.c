// File_system_server.c
// Flat filesystem server for Project 3 - Part 

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

#define BLOCK_SIZE      128
#define TOTAL_BLOCKS    1024

#define FAT_BLOCKS      32
#define DIR_BLOCKS      32

#define SUPERBLOCK_BLOCK 0
#define FAT_START_BLOCK  (SUPERBLOCK_BLOCK + 1)
#define DIR_START_BLOCK  (FAT_START_BLOCK + FAT_BLOCKS)
#define DATA_START_BLOCK (DIR_START_BLOCK + DIR_BLOCKS)

// FAT markers
#define FAT_FREE     (-1)
#define FAT_EOF      (-2)
#define FAT_RESERVED (-3)

#define MAX_FILENAME 32
#define DIR_ENTRIES  64   // 32 blocks * 128 bytes / 64 bytes per entry

typedef struct {
    char magic[4];      // "FS01"
    int total_blocks;
    int fat_start;
    int fat_blocks;
    int dir_start;
    int dir_blocks;
    int data_start;
    int reserved[25];   // padding to fit one 128-byte block
} Superblock;

typedef struct {
    char name[MAX_FILENAME]; // 32 bytes
    int length;              // file length in bytes
    int first_block;         // index into FAT of first data block, or -1
    int in_use;              // 0 = free, 1 = used
    char padding[20];        // pad struct to 64 bytes
} DirEntry;

static int fs_fd = -1;
static Superblock super;
static int fat[TOTAL_BLOCKS];
static DirEntry dir_table[DIR_ENTRIES];
static int fs_formatted = 0;

// Low-level disk helpers

static off_t block_offset(int block_index) {
    return (off_t)block_index * BLOCK_SIZE;
}

static void die(const char *msg) {
    perror(msg);
    exit(1);
}

// Filesystem metadata load/save

static int load_superblock() {
    if (lseek(fs_fd, block_offset(SUPERBLOCK_BLOCK), SEEK_SET) < 0)
        return 0;
    ssize_t n = read(fs_fd, &super, sizeof(Superblock));
    if (n != sizeof(Superblock))
        return 0;
    if (memcmp(super.magic, "FS01", 4) != 0)
        return 0;
    return 1;
}

static void save_superblock() {
    if (lseek(fs_fd, block_offset(SUPERBLOCK_BLOCK), SEEK_SET) < 0)
        die("lseek super");
    if (write(fs_fd, &super, sizeof(Superblock)) != sizeof(Superblock))
        die("write super");
}

static void load_fat() {
    if (lseek(fs_fd, block_offset(FAT_START_BLOCK), SEEK_SET) < 0)
        die("lseek fat");
    ssize_t n = read(fs_fd, fat, sizeof(fat));
    if (n != sizeof(fat))
        die("read fat");
}

static void save_fat() {
    if (lseek(fs_fd, block_offset(FAT_START_BLOCK), SEEK_SET) < 0)
        die("lseek fat write");
    if (write(fs_fd, fat, sizeof(fat)) != sizeof(fat))
        die("write fat");
}

static void load_dir() {
    if (lseek(fs_fd, block_offset(DIR_START_BLOCK), SEEK_SET) < 0)
        die("lseek dir");
    ssize_t n = read(fs_fd, dir_table, sizeof(dir_table));
    if (n != sizeof(dir_table))
        die("read dir");
}

static void save_dir() {
    if (lseek(fs_fd, block_offset(DIR_START_BLOCK), SEEK_SET) < 0)
        die("lseek dir write");
    if (write(fs_fd, dir_table, sizeof(dir_table)) != sizeof(dir_table))
        die("write dir");
}

// Formatting

static int fs_format() {
    // Fill superblock
    memcpy(super.magic, "FS01", 4);
    super.total_blocks = TOTAL_BLOCKS;
    super.fat_start = FAT_START_BLOCK;
    super.fat_blocks = FAT_BLOCKS;
    super.dir_start = DIR_START_BLOCK;
    super.dir_blocks = DIR_BLOCKS;
    super.data_start = DATA_START_BLOCK;
    memset(super.reserved, 0, sizeof(super.reserved));

    save_superblock();

    // Initialize FAT
    for (int i = 0; i < TOTAL_BLOCKS; i++) {
        if (i < DATA_START_BLOCK) {
            fat[i] = FAT_RESERVED; // space used by superblock/FAT/dir
        } else {
            fat[i] = FAT_FREE;
        }
    }
    save_fat();

    // Initialize directory
    for (int i = 0; i < DIR_ENTRIES; i++) {
        dir_table[i].in_use = 0;
        dir_table[i].name[0] = '\0';
        dir_table[i].length = 0;
        dir_table[i].first_block = -1;
        memset(dir_table[i].padding, 0, sizeof(dir_table[i].padding));
    }
    save_dir();

    // Zero data blocks (not strictly required but nice)
    unsigned char zero[BLOCK_SIZE];
    memset(zero, 0, BLOCK_SIZE);
    for (int b = DATA_START_BLOCK; b < TOTAL_BLOCKS; b++) {
        if (lseek(fs_fd, block_offset(b), SEEK_SET) < 0)
            die("lseek data zero");
        if (write(fs_fd, zero, BLOCK_SIZE) != BLOCK_SIZE)
            die("write data zero");
    }

    fs_formatted = 1;
    return 0; // success
}

static void fs_load_or_unformatted() {
    if (!load_superblock()) {
        fs_formatted = 0;
        return;
    }
    // If superblock looks good, load FAT and directory
    load_fat();
    load_dir();
    fs_formatted = 1;
}

// Helper: find file, allocate blocks, free blocks

static int find_file(const char *name) {
    for (int i = 0; i < DIR_ENTRIES; i++) {
        if (dir_table[i].in_use && strcmp(dir_table[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int alloc_block() {
    for (int i = DATA_START_BLOCK; i < TOTAL_BLOCKS; i++) {
        if (fat[i] == FAT_FREE) {
            fat[i] = FAT_EOF; // mark as end-of-chain for now
            return i;
        }
    }
    return -1; // no space
}

static void free_chain(int first_block) {
    int cur = first_block;
    while (cur >= DATA_START_BLOCK && cur < TOTAL_BLOCKS) {
        int next = fat[cur];
        fat[cur] = FAT_FREE;
        if (next == FAT_EOF || next == FAT_FREE || next == FAT_RESERVED)
            break;
        cur = next;
    }
}

// FS operations implementing the prompt

static int fs_create(const char *name) {
    if (!fs_formatted) return 2;
    if (strlen(name) >= MAX_FILENAME) return 2;

    if (find_file(name) >= 0)
        return 1; // already exists

    // find free dir entry
    for (int i = 0; i < DIR_ENTRIES; i++) {
        if (!dir_table[i].in_use) {
            dir_table[i].in_use = 1;
            strncpy(dir_table[i].name, name, MAX_FILENAME - 1);
            dir_table[i].name[MAX_FILENAME - 1] = '\0';
            dir_table[i].length = 0;
            dir_table[i].first_block = -1;
            save_dir();
            save_fat(); // not really changed, but safe
            return 0;
        }
    }
    return 2; // no directory space
}

static int fs_delete(const char *name) {
    if (!fs_formatted) return 2;

    int idx = find_file(name);
    if (idx < 0)
        return 1;

    if (dir_table[idx].first_block >= 0) {
        free_chain(dir_table[idx].first_block);
    }

    dir_table[idx].in_use = 0;
    dir_table[idx].name[0] = '\0';
    dir_table[idx].length = 0;
    dir_table[idx].first_block = -1;

    save_dir();
    save_fat();
    return 0;
}

static int fs_write(const char *name, const unsigned char *data, int len) {
    if (!fs_formatted) return 2;

    int idx = find_file(name);
    if (idx < 0)
        return 1; // no such filename

    // free old chain
    if (dir_table[idx].first_block >= 0) {
        free_chain(dir_table[idx].first_block);
    }

    if (len == 0) {
        dir_table[idx].first_block = -1;
        dir_table[idx].length = 0;
        save_dir();
        save_fat();
        return 0;
    }

    int blocks_needed = (len + BLOCK_SIZE - 1) / BLOCK_SIZE;
    int first = -1;
    int prev = -1;
    int pos = 0;

    for (int i = 0; i < blocks_needed; i++) {
        int b = alloc_block();
        if (b < 0) {
            // out of space – free what we allocated so far
            if (first >= 0) free_chain(first);
            return 2;
        }

        if (first < 0) first = b;
        if (prev >= 0) fat[prev] = b;
        fat[b] = FAT_EOF;
        prev = b;

        // write up to BLOCK_SIZE bytes of data into this block
        unsigned char buf[BLOCK_SIZE];
        memset(buf, 0, BLOCK_SIZE);
        int chunk = BLOCK_SIZE;
        if (len - pos < BLOCK_SIZE) chunk = len - pos;
        memcpy(buf, data + pos, chunk);
        pos += chunk;

        if (lseek(fs_fd, block_offset(b), SEEK_SET) < 0)
            die("lseek data write");
        if (write(fs_fd, buf, BLOCK_SIZE) != BLOCK_SIZE)
            die("write data block");
    }

    dir_table[idx].first_block = first;
    dir_table[idx].length = len;
    save_fat();
    save_dir();
    return 0;
}

static int fs_read(const char *name, unsigned char **out_buf, int *out_len) {
    if (!fs_formatted) return 2;

    int idx = find_file(name);
    if (idx < 0)
        return 1;

    int len = dir_table[idx].length;
    *out_len = len;

    if (len == 0) {
        *out_buf = NULL;
        return 0;
    }

    unsigned char *buf = (unsigned char *)malloc(len);
    if (!buf) return 2;

    int pos = 0;
    int cur = dir_table[idx].first_block;

    while (cur >= DATA_START_BLOCK && cur < TOTAL_BLOCKS && pos < len) {
        unsigned char block[BLOCK_SIZE];
        if (lseek(fs_fd, block_offset(cur), SEEK_SET) < 0) {
            free(buf);
            return 2;
        }
        if (read(fs_fd, block, BLOCK_SIZE) != BLOCK_SIZE) {
            free(buf);
            return 2;
        }
        int chunk = BLOCK_SIZE;
        if (len - pos < BLOCK_SIZE) chunk = len - pos;
        memcpy(buf + pos, block, chunk);
        pos += chunk;

        int next = fat[cur];
        if (next == FAT_EOF || next == FAT_FREE || next == FAT_RESERVED)
            break;
        cur = next;
    }

    *out_buf = buf;
    return 0;
}

// Network handling for FS protocol

static void handle_list(FILE *client, int verbose) {
    // status line just to keep consistent
    fprintf(client, "0\n");
    for (int i = 0; i < DIR_ENTRIES; i++) {
        if (dir_table[i].in_use) {
            if (!verbose)
                fprintf(client, "%s\n", dir_table[i].name);
            else
                fprintf(client, "%s %d\n", dir_table[i].name, dir_table[i].length);
        }
    }
    fprintf(client, "END\n");
    fflush(client);
}

static ssize_t read_exact(int fd, unsigned char *buf, int len) {
    int total = 0;
    while (total < len) {
        ssize_t n = read(fd, buf + total, len - total);
        if (n <= 0) return n;
        total += n;
    }
    return total;
}

static void handle_client(int client_sock) {
    FILE *client = fdopen(client_sock, "r+");
    if (!client) {
        perror("fdopen client");
        close(client_sock);
        return;
    }

    char line[1024];

    while (fgets(line, sizeof(line), client) != NULL) {
        if (line[0] == 'F') {
            int rc = fs_format();
            fprintf(client, "%d\n", rc);
            fflush(client);
        } else if (line[0] == 'C') {
            char fname[MAX_FILENAME];
            if (sscanf(line, "C %31s", fname) != 1) {
                fprintf(client, "2\n");
                fflush(client);
                continue;
            }
            int rc = fs_create(fname);
            fprintf(client, "%d\n", rc);
            fflush(client);
        } else if (line[0] == 'D') {
            char fname[MAX_FILENAME];
            if (sscanf(line, "D %31s", fname) != 1) {
                fprintf(client, "2\n");
                fflush(client);
                continue;
            }
            int rc = fs_delete(fname);
            fprintf(client, "%d\n", rc);
            fflush(client);
        } else if (line[0] == 'L') {
            int b = 0;
            sscanf(line, "L %d", &b);
            handle_list(client, b != 0);
        } else if (line[0] == 'R') {
            char fname[MAX_FILENAME];
            if (sscanf(line, "R %31s", fname) != 1) {
                fprintf(client, "2 0 \n");
                fflush(client);
                continue;
            }
            unsigned char *buf = NULL;
            int len = 0;
            int rc = fs_read(fname, &buf, &len);
            // Send: return_code, length (ASCII), space, data
            fprintf(client, "%d %d ", rc, len);
            fflush(client);
            if (rc == 0 && len > 0 && buf != NULL) {
                int fd = fileno(client);
                if (write(fd, buf, len) != len) {
                    perror("write read-data to client");
                }
            }
            fputc('\n', client); // line break after data
            fflush(client);
            if (buf) free(buf);
        } else if (line[0] == 'W') {
            char fname[MAX_FILENAME];
            int len;
            if (sscanf(line, "W %31s %d", fname, &len) != 2 || len < 0) {
                fprintf(client, "2\n");
                fflush(client);
                continue;
            }
            unsigned char *buf = NULL;
            if (len > 0) {
                buf = (unsigned char *)malloc(len);
                if (!buf) {
                    fprintf(client, "2\n");
                    fflush(client);
                    continue;
                }
                int fd = fileno(client);
                if (read_exact(fd, buf, len) != len) {
                    free(buf);
                    fprintf(client, "2\n");
                    fflush(client);
                    continue;
                }
            }
            int rc = fs_write(fname, buf, len);
            if (buf) free(buf);
            fprintf(client, "%d\n", rc);
            fflush(client);
        } else {
            // Unknown command
            fprintf(client, "2\n");
            fflush(client);
        }
    }

    fclose(client);
}

// main

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <port> <fs_image>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    const char *fs_image = argv[2];

    fs_fd = open(fs_image, O_RDWR | O_CREAT, 0666);
    if (fs_fd < 0) die("open fs_image");

    // Ensure size
    off_t size = (off_t)TOTAL_BLOCKS * BLOCK_SIZE;
    if (ftruncate(fs_fd, size) < 0) die("ftruncate fs_image");

    fs_load_or_unformatted();
    if (!fs_formatted) {
        fprintf(stderr, "Filesystem not formatted yet. Use 'F' command from client.\n");
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) die("socket");

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        die("bind");

    if (listen(listen_fd, 5) < 0)
        die("listen");

    printf("Filesystem server listening on port %d, image %s\n", port, fs_image);

    while (1) {
        int client_sock = accept(listen_fd, NULL, NULL);
        if (client_sock < 0) {
            perror("accept");
            continue;
        }
        // Single-threaded: handle one client at a time
        handle_client(client_sock);
    }

    close(listen_fd);
    close(fs_fd);
    return 0;
}
