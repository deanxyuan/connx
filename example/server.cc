/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifdef _WIN32
#    include <winsock2.h>
#    include <ws2tcpip.h>
#else
#    include <arpa/inet.h>
#    include <netinet/in.h>
#    include <sys/select.h>
#    include <sys/socket.h>
#    include <unistd.h>
#    include <errno.h>
#    include <signal.h>
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define BUF_SIZE    4096
#define MAX_CLIENTS (FD_SETSIZE - 1)
#ifdef _WIN32
#    define CLOSE_SOCKET closesocket
typedef int socklen_t;
#else
#    define CLOSE_SOCKET   close
#    define INVALID_SOCKET (-1)
typedef int SOCKET;
#endif

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "invalid port: %s\n", argv[1]);
        return 1;
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    SOCKET listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == INVALID_SOCKET) {
        fprintf(stderr, "socket failed: %s\n", strerror(errno));
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#ifdef SO_NOSIGPIPE
    setsockopt(listen_fd, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);
    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "bind failed: %s\n", strerror(errno));
        CLOSE_SOCKET(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 128) != 0) {
        fprintf(stderr, "listen failed: %s\n", strerror(errno));
        CLOSE_SOCKET(listen_fd);
        return 1;
    }

    printf("echo server listening on port %d\n", port);

    SOCKET clients[FD_SETSIZE];
    for (int i = 0; i < FD_SETSIZE; i++) {
        clients[i] = INVALID_SOCKET;
    }

    int max_i = -1;
    SOCKET max_fd = listen_fd;
    fd_set all_fds;
    FD_ZERO(&all_fds);
    FD_SET(listen_fd, &all_fds);

    for (;;) {
        fd_set read_fds = all_fds;
        int nready = select((int)max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (nready < 0) {
            fprintf(stderr, "select failed: %s\n", strerror(errno));
            break;
        }

        // New connection
        if (FD_ISSET(listen_fd, &read_fds)) {
            struct sockaddr_in cli_addr;
            socklen_t cli_len = sizeof(cli_addr);
            SOCKET cli_fd = accept(listen_fd, (struct sockaddr*)&cli_addr, &cli_len);
            if (cli_fd == INVALID_SOCKET) {
                fprintf(stderr, "accept failed: %s\n", strerror(errno));
            } else {
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &cli_addr.sin_addr, ip, sizeof(ip));
                printf("client connected [%s:%d] (fd=%d)\n", ip, ntohs(cli_addr.sin_port),
                       (int)cli_fd);
                int slot = -1;
                for (int i = 0; i < FD_SETSIZE; i++) {
                    if (clients[i] == INVALID_SOCKET) {
                        slot = i;
                        break;
                    }
                }

                if (slot < 0) {
                    printf("too many connections, rejecting\n");
                    CLOSE_SOCKET(cli_fd);
                } else {
                    clients[slot] = cli_fd;
                    FD_SET(cli_fd, &all_fds);
                    if (cli_fd > max_fd) max_fd = cli_fd;
                    if (slot > max_i) max_i = slot;
                }
            }
            if (--nready <= 0) continue;
        }

        // Client data
        char buf[BUF_SIZE];
        for (int i = 0; i <= max_i && nready > 0; i++) {
            SOCKET fd = clients[i];
            if (fd == INVALID_SOCKET || !FD_ISSET(fd, &read_fds)) continue;
            int n = (int)recv(fd, buf, BUF_SIZE, 0);
            if (n <= 0) {
                printf("client disconnected (fd=%d)\n", (int)fd);
                FD_CLR(fd, &all_fds);
                CLOSE_SOCKET(fd);
                clients[i] = INVALID_SOCKET;
            } else {
                // Echo back
                int sent = (int)send(fd, buf, n, 0);
                if (sent != n) {
                    printf("send error on fd=%d\n", (int)fd);
                    FD_CLR(fd, &all_fds);
                    CLOSE_SOCKET(fd);
                    clients[i] = INVALID_SOCKET;
                }
            }
            nready--;
        }
    }

    CLOSE_SOCKET(listen_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
