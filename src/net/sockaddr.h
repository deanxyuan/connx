/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_NET_SOCKADDR_H_
#define CONNX_SRC_NET_SOCKADDR_H_

#ifdef _WIN32
#    include <winsock2.h>
#    include <ws2tcpip.h>
#    include <mswsock.h>
#else
#    include <arpa/inet.h>
#    include <netdb.h>
#    include <netinet/in.h>
#    include <netinet/tcp.h>
#    include <sys/socket.h>
#    include <sys/types.h>
#    include <unistd.h>
#endif

typedef struct sockaddr connx_sockaddr;
typedef struct sockaddr_in connx_sockaddr_in;
typedef struct in_addr connx_in_addr;
typedef struct sockaddr_in6 connx_sockaddr_in6;
typedef struct in6_addr connx_in6_addr;

#endif // CONNX_SRC_NET_SOCKADDR_H_
