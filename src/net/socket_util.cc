/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "src/net/socket_util.h"

#include <stdint.h>
#include <string.h>

#include "src/net/sockaddr.h"
#include "src/utils/useful.h"

#ifdef _WIN32
const char* connx_inet_ntop(int af, const void* src, char* dst, size_t size) {
    /* Windows InetNtopA wants a mutable ip pointer */
    return InetNtopA(af, (void*)src, dst, size);
}
#else
const char* connx_inet_ntop(int af, const void* src, char* dst, size_t size) {
    CONNX_ASSERT(size <= (socklen_t)-1);
    return inet_ntop(af, src, dst, static_cast<socklen_t>(size));
}
#endif

void connx_sockaddr_make_wildcard4(int port, connx_resolved_address* resolved_wild_out) {
    connx_sockaddr_in* wild_out = reinterpret_cast<connx_sockaddr_in*>(resolved_wild_out->addr);
    CONNX_ASSERT(port >= 0 && port < 65536);
    memset(resolved_wild_out, 0, sizeof(*resolved_wild_out));
    wild_out->sin_family = AF_INET;
    wild_out->sin_port = htons(static_cast<uint16_t>(port));
    resolved_wild_out->len = static_cast<socklen_t>(sizeof(connx_sockaddr_in));
}

int connx_string_to_sockaddr(connx_resolved_address* out, const char* addr, int port) {
    memset(out, 0, sizeof(connx_resolved_address));
    connx_sockaddr_in6* addr6 = (connx_sockaddr_in6*)out->addr;
    connx_sockaddr_in* addr4 = (connx_sockaddr_in*)out->addr;
    if (inet_pton(AF_INET6, addr, &addr6->sin6_addr) == 1) {
        addr6->sin6_family = AF_INET6;
        out->len = sizeof(connx_sockaddr_in6);
    } else if (inet_pton(AF_INET, addr, &addr4->sin_addr) == 1) {
        addr4->sin_family = AF_INET;
        out->len = sizeof(connx_sockaddr_in);
    } else {
        return 0;
    }
    connx_sockaddr_set_port(out, port);
    return 1;
}

int connx_sockaddr_get_port(const connx_resolved_address* resolved_addr) {
    const connx_sockaddr* addr = reinterpret_cast<const connx_sockaddr*>(resolved_addr->addr);
    switch (addr->sa_family) {
    case AF_INET:
        return ntohs(((connx_sockaddr_in*)addr)->sin_port);
    case AF_INET6:
        return ntohs(((connx_sockaddr_in6*)addr)->sin6_port);
    default:
        CONNX_LOG_WARN("Unknown socket family %d in connx_sockaddr_get_port", addr->sa_family);
        return 0;
    }
}

int connx_sockaddr_set_port(const connx_resolved_address* resolved_addr, int port) {
    const connx_sockaddr* addr = reinterpret_cast<const connx_sockaddr*>(resolved_addr->addr);
    if (!(port >= 0 && port < 65536)) {
        CONNX_LOG_ERROR("Invalid port number: %d", port);
        return 0;
    }

    switch (addr->sa_family) {
    case AF_INET:
        ((connx_sockaddr_in*)addr)->sin_port = htons(static_cast<uint16_t>(port));
        return 1;
    case AF_INET6:
        ((connx_sockaddr_in6*)addr)->sin6_port = htons(static_cast<uint16_t>(port));
        return 1;
    default:
        CONNX_LOG_ERROR("Unknown socket family %d in connx_sockaddr_set_port", addr->sa_family);
        return 0;
    }
}
