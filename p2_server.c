// p2_server.c - Fork + exec ls server
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>

#define BUF_SIZE 1024
#define MAX_ARGS 64

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
        int clientfd = accept(serverfd, (struct sockaddr *)&client, &len);
        if (clientfd < 0) {
            perror("accept");
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close(clientfd);
            continue;
        }

        if (pid == 0) {
            // Child process
            char buf[BUF_SIZE];
            int n = recv(clientfd, buf, BUF_SIZE - 1, 0);
            if (n < 0) {
                perror("recv");
                close(clientfd);
                exit(1);
            }
            buf[n] = '\0';

            char *args[MAX_ARGS];
            int c = 0;
            args[c++] = "ls";

            char *saveptr;
            char *tok = strtok_r(buf, " \t\r\n", &saveptr);
            while (tok && c < MAX_ARGS - 1) {
                args[c++] = tok;
                tok = strtok_r(NULL, " \t\r\n", &saveptr);
            }
            args[c] = NULL;

            dup2(clientfd, STDOUT_FILENO);
            dup2(clientfd, STDERR_FILENO);

            execvp("ls", args);
            perror("execvp");
            close(clientfd);
            exit(1);
        } else {
            // Parent
            close(clientfd);
            while (waitpid(-1, NULL, WNOHANG) > 0) {
                // reap children
            }
        }
    }

    close(serverfd);
    return 0;
}
