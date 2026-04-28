/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "test/support/echoserver.h"

#ifdef _WIN32
#    include <winsock2.h>
#    include <ws2tcpip.h>
#    include <windows.h>
typedef int socklen_t;
#    define INVALID_SOCK INVALID_SOCKET
#    define CLOSE_SOCKET closesocket
#else
#    include <arpa/inet.h>
#    include <errno.h>
#    include <netinet/in.h>
#    include <sys/select.h>
#    include <sys/socket.h>
#    include <unistd.h>
typedef int SOCKET;
#    define INVALID_SOCK (-1)
#    define CLOSE_SOCKET close
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUF_SIZE     4096
#define MAX_CLIENTS  (FD_SETSIZE - 1)
#define SELECT_TO_MS 100 // poll interval for clean shutdown

namespace test {

EchoServer::EchoServer()
    :
#ifdef _WIN32
    wsa_initialized_(false)
    , listen_fd_(INVALID_SOCK)
#else
    listen_fd_(INVALID_SOCK)
#endif
    , port_(-1)
    , running_(false) {
}

EchoServer::~EchoServer() { Stop(); }

int EchoServer::Start(int port) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "echoserver: WSAStartup failed\n");
        return -1;
    }
    wsa_initialized_ = true;

    SOCKET fd = socket(AF_INET, SOCK_STREAM, 0);
#else
    SOCKET fd = socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (fd == INVALID_SOCK) {
        perror("echoserver: socket");
        goto fail;
    }

    {
        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    }

    {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons((unsigned short)port);

        if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
            perror("echoserver: bind");
            CLOSE_SOCKET(fd);
            goto fail;
        }

        if (listen(fd, 128) != 0) {
            perror("echoserver: listen");
            CLOSE_SOCKET(fd);
            goto fail;
        }

        // Read back the actual port (needed when port 0 was passed).
        socklen_t addr_len = sizeof(addr);
        if (getsockname(fd, (struct sockaddr*)&addr, &addr_len) != 0) {
            perror("echoserver: getsockname");
            CLOSE_SOCKET(fd);
            goto fail;
        }
        port_ = ntohs(addr.sin_port);
    }

#ifdef _WIN32
    listen_fd_ = fd;
#else
    listen_fd_ = fd;
#endif

    running_ = true;
    thread_ = std::thread(&EchoServer::Run, this);
    return port_;

fail:
#ifdef _WIN32
    if (wsa_initialized_) {
        WSACleanup();
        wsa_initialized_ = false;
    }
#endif
    return -1;
}

void EchoServer::Stop() {
    if (!running_.exchange(false)) return;

    if (thread_.joinable()) {
        thread_.join();
    }

#ifdef _WIN32
    if (listen_fd_ != INVALID_SOCK) {
        CLOSE_SOCKET((SOCKET)listen_fd_);
        listen_fd_ = INVALID_SOCK;
    }
    if (wsa_initialized_) {
        WSACleanup();
        wsa_initialized_ = false;
    }
#else
    CLOSE_SOCKET(listen_fd_);
    listen_fd_ = INVALID_SOCK;
#endif
}

void EchoServer::Run() {
    SOCKET clients[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i] = INVALID_SOCK;
    }

#ifdef _WIN32
    SOCKET lfd = (SOCKET)listen_fd_;
#else
    SOCKET lfd = listen_fd_;
#endif

    int max_i = -1;
    SOCKET max_fd = lfd;

    fd_set all_fds;
    FD_ZERO(&all_fds);
    FD_SET(lfd, &all_fds);

    struct timeval tv;

    while (running_) {
        fd_set read_fds = all_fds;
        tv.tv_sec = 0;
        tv.tv_usec = SELECT_TO_MS * 1000;

        int nready = select((int)max_fd + 1, &read_fds, NULL, NULL, &tv);
        if (nready < 0) {
#ifdef _WIN32
            break;
#else
            if (errno == EINTR) continue;
            break;
#endif
        }

        if (!running_) break;

        // New connection.
        if (FD_ISSET(lfd, &read_fds)) {
            struct sockaddr_in cli_addr;
            socklen_t cli_len = sizeof(cli_addr);
            SOCKET cli_fd = accept(lfd, (struct sockaddr*)&cli_addr, &cli_len);
            if (cli_fd != INVALID_SOCK) {
                int slot = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i] == INVALID_SOCK) {
                        slot = i;
                        break;
                    }
                }

                if (slot < 0) {
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

        // Client data — echo back.
        char buf[BUF_SIZE];
        for (int i = 0; i <= max_i && nready > 0; i++) {
            SOCKET fd = clients[i];
            if (fd == INVALID_SOCK || !FD_ISSET(fd, &read_fds)) continue;

            int n = (int)recv(fd, buf, BUF_SIZE, 0);
            if (n <= 0) {
                FD_CLR(fd, &all_fds);
                CLOSE_SOCKET(fd);
                clients[i] = INVALID_SOCK;
            } else {
                int sent = 0;
                while (sent < n) {
                    int r = (int)send(fd, buf + sent, n - sent, 0);
                    if (r <= 0) {
                        FD_CLR(fd, &all_fds);
                        CLOSE_SOCKET(fd);
                        clients[i] = INVALID_SOCK;
                        break;
                    }
                    sent += r;
                }
            }
            nready--;
        }
    }

    // Clean up remaining client sockets.
    for (int i = 0; i <= max_i; i++) {
        if (clients[i] != INVALID_SOCK) {
            CLOSE_SOCKET(clients[i]);
        }
    }
}

} // namespace test
