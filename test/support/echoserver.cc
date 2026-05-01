/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "test/support/echoserver.h"

#ifdef _WIN32
#    include <winsock2.h>
#    include <ws2tcpip.h>
typedef int socklen_t;
#    define CLOSE_SOCKET closesocket
#else
#    include <arpa/inet.h>
#    include <errno.h>
#    include <netinet/in.h>
#    include <sys/select.h>
#    include <sys/socket.h>
#    include <unistd.h>
typedef int SOCKET;
#    define INVALID_SOCKET (-1)
#    define CLOSE_SOCKET   close
#endif

#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>

#define BUF_SIZE     4096
#define MAX_CLIENTS  (FD_SETSIZE - 1)
#define SELECT_TO_MS 100 // poll interval so running_ checks are responsive

namespace test {
namespace {

std::atomic<bool> running_{false};
std::thread       thread_;
int               port_ = -1;

#ifdef _WIN32
SOCKET  listen_fd_ = INVALID_SOCKET;
bool    wsa_ready_ = false;
#else
int     listen_fd_ = INVALID_SOCKET;
#endif

void RunEchoLoop() {
    SOCKET clients[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i] = INVALID_SOCKET;
    }

    int max_i = -1;
    SOCKET max_fd = listen_fd_;

    fd_set all_fds;
    FD_ZERO(&all_fds);
    FD_SET(listen_fd_, &all_fds);

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

        // New connection.
        if (FD_ISSET(listen_fd_, &read_fds)) {
            struct sockaddr_in cli_addr;
            socklen_t cli_len = sizeof(cli_addr);
            SOCKET cli_fd = accept(listen_fd_, (struct sockaddr*)&cli_addr, &cli_len);
            if (cli_fd == INVALID_SOCKET) {
                // On Win32 this is likely WSAEWOULDBLOCK; just skip.
            } else {
                int slot = -1;
                for (int i = 0; i < FD_SETSIZE; i++) {
                    if (clients[i] == INVALID_SOCKET) {
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

        // Client data - echo back.
        char buf[BUF_SIZE];
        for (int i = 0; i <= max_i && nready > 0; i++) {
            SOCKET fd = clients[i];
            if (fd == INVALID_SOCKET || !FD_ISSET(fd, &read_fds)) continue;

            int n = (int)recv(fd, buf, BUF_SIZE, 0);
            if (n <= 0) {
                FD_CLR(fd, &all_fds);
                CLOSE_SOCKET(fd);
                clients[i] = INVALID_SOCKET;
            } else {
                int sent = 0;
                while (sent < n) {
                    int r = (int)send(fd, buf + sent, n - sent, 0);
                    if (r <= 0) {
                        FD_CLR(fd, &all_fds);
                        CLOSE_SOCKET(fd);
                        clients[i] = INVALID_SOCKET;
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
        if (clients[i] != INVALID_SOCKET) {
            CLOSE_SOCKET(clients[i]);
        }
    }
}

} // namespace

int StartEchoServer(int port) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "echoserver: WSAStartup failed\n");
        return -1;
    }
    wsa_ready_ = true;
#endif

    SOCKET listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == INVALID_SOCKET) {
#ifdef _WIN32
        fprintf(stderr, "echoserver: socket failed: %ld\n", WSAGetLastError());
#else
        perror("echoserver: socket");
#endif
        return -1;
    }

    {
        int opt = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#ifdef SO_NOSIGPIPE
        setsockopt(listen_fd, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
#endif
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
#ifdef _WIN32
        fprintf(stderr, "echoserver: bind failed: %ld\n", WSAGetLastError());
#else
        perror("echoserver: bind");
#endif
        CLOSE_SOCKET(listen_fd);
        return -1;
    }

    if (listen(listen_fd, 128) != 0) {
#ifdef _WIN32
        fprintf(stderr, "echoserver: listen failed: %ld\n", WSAGetLastError());
#else
        perror("echoserver: listen");
#endif
        CLOSE_SOCKET(listen_fd);
        return -1;
    }

    // Read back the actual port (needed when port 0 was passed).
    {
        socklen_t addr_len = sizeof(addr);
        if (getsockname(listen_fd, (struct sockaddr*)&addr, &addr_len) != 0) {
#ifdef _WIN32
            fprintf(stderr, "echoserver: getsockname failed: %ld\n", WSAGetLastError());
#else
            perror("echoserver: getsockname");
#endif
            CLOSE_SOCKET(listen_fd);
            return -1;
        }
        port_ = ntohs(addr.sin_port);
    }

    listen_fd_ = listen_fd;
    running_ = true;
    thread_ = std::thread(RunEchoLoop);
    return port_;
}

void StopEchoServer() {
    running_ = false;

    if (thread_.joinable()) {
        thread_.join();
    }

    CLOSE_SOCKET(listen_fd_);
    listen_fd_ = INVALID_SOCKET;
    port_ = -1;

#ifdef _WIN32
    if (wsa_ready_) {
        WSACleanup();
        wsa_ready_ = false;
    }
#endif
}

int EchoServerPort() { return port_; }

} // namespace test
