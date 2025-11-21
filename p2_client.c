// p2_client.c - Sends ls args and prints output
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BUF_SIZE 1024

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port> [ls_args...]\n", argv[0]);
        return 1;
    }

    char *ip = argv[1];
    int port = atoi(argv[2]);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &serv.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    char buf[BUF_SIZE] = {0};

    for (int i = 3; i < argc; i++) {
        strncat(buf, argv[i], sizeof(buf) - strlen(buf) - 2);
        if (i < argc - 1) strncat(buf, " ", sizeof(buf) - strlen(buf) - 2);
    }
    strncat(buf, "\n", sizeof(buf) - strlen(buf) - 1);

    if (send(sock, buf, strlen(buf), 0) < 0) {
        perror("send");
        close(sock);
        return 1;
    }

    int n;
    while ((n = recv(sock, buf, BUF_SIZE - 1, 0)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }

    if (n < 0) {
        perror("recv");
    }

    close(sock);
    return 0;
}
