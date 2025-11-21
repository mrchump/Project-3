// p1_server.c - Multithreaded reverse-string server
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define BUF_SIZE 1024

void reverse(char *s) {
    int i = 0, j = (int)strlen(s) - 1;
    if (j >= 0 && s[j] == '\n') j--;
    while (i < j) {
        char t = s[i];
        s[i] = s[j];
        s[j] = t;
        i++; j--;
    }
}

void *handle_client(void *arg) {
    int clientfd = *(int *)arg;
    free(arg);

    char buf[BUF_SIZE];
    int n = recv(clientfd, buf, BUF_SIZE - 1, 0);
    if (n > 0) {
        buf[n] = '\0';
        reverse(buf);
        send(clientfd, buf, strlen(buf), 0);
    }

    close(clientfd);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    int serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverfd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(serverfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(serverfd);
        return 1;
    }

    if (listen(serverfd, 10) < 0) {
        perror("listen");
        close(serverfd);
        return 1;
    }

    while (1) {
        struct sockaddr_in client;
        socklen_t len = sizeof(client);
        int *clientfd = malloc(sizeof(int));
        if (!clientfd) {
            perror("malloc");
            break;
        }
        *clientfd = accept(serverfd, (struct sockaddr *)&client, &len);
        if (*clientfd < 0) {
            perror("accept");
            free(clientfd);
            continue;
        }

        pthread_t t;
        if (pthread_create(&t, NULL, handle_client, clientfd) != 0) {
            perror("pthread_create");
            close(*clientfd);
            free(clientfd);
            continue;
        }
        pthread_detach(t);
    }

    close(serverfd);
    return 0;
}
