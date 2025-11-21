// p1_client.c - Sends a string, receives reversed string
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server_ip> <port> \"<message>\"\n", argv[0]);
        return 1;
    }

    char *ip = argv[1];
    int port = atoi(argv[2]);
    char *msg = argv[3];

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

    char buf[1024];
    snprintf(buf, sizeof(buf), "%s\n", msg);

    if (send(sock, buf, strlen(buf), 0) < 0) {
        perror("send");
        close(sock);
        return 1;
    }

    int n = recv(sock, buf, sizeof(buf) - 1, 0);
    if (n < 0) {
        perror("recv");
        close(sock);
        return 1;
    }
    buf[n] = '\0';

    printf("Reversed: %s", buf);

    close(sock);
    return 0;
}
