/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "src/net/resolve_address.h"
#include "src/utils/useful.h"
#include "src/utils/string.h"
#include "src/utils/status.h"
#include "src/net/sockaddr.h"

#ifndef _WIN32
static connx_error connx_getaddrinfo_error(int err) {
    char* message = nullptr;
    int n = connx_format(&message, "'getaddrinfo'-[%s]", gai_strerror(err));
    if (!message) return CONNX_ERROR_NONE;
    std::string str = (n > 0) ? std::string(message, n) : std::string();
    auto obj = std::make_shared<connx::Status>(err, str);
    free(message);
    return obj;
}
#endif

int connx_join_host_port(char** out, const char* host, int port) {
    int ret;
    if (host[0] != '[' && strchr(host, ':') != nullptr) {
        /* IPv6 literals must be enclosed in brackets. */
        ret = connx_format(out, "[%s]:%d", host, port);
    } else {
        /* Ordinary non-bracketed host:port. */
        ret = connx_format(out, "%s:%d", host, port);
    }
    return ret;
}

bool connx_split_host_port(const std::string& name, std::string* host, std::string* port) {
    if (name[0] == '[') {
        /* Parse a bracketed host, typically an IPv6 literal. */
        const size_t rbracket = name.find(']', 1);
        if (rbracket == std::string::npos) {
            /* Unmatched [ */
            return false;
        }
        if (rbracket == name.size() - 1) {
            /* ]<end> */
            *port = std::string();
        } else if (name[rbracket + 1] == ':') {
            /* ]:<port?> */
            *port = name.substr(rbracket + 2, name.size() - rbracket - 2);
        } else {
            /* ]<invalid> */
            return false;
        }
        *host = name.substr(1, rbracket - 1);
        if (host->find(':') == std::string::npos) {
            /* Require all bracketed hosts to contain a colon, because a hostname or
                IPv4 address should never use brackets. */
            *host = std::string();
            return false;
        }
    } else {
        size_t colon = name.find(':');
        if (colon != std::string::npos && name.find(':', colon + 1) == std::string::npos) {
            /* Exactly 1 colon.  Split into host:port. */
            *host = name.substr(0, colon);
            *port = name.substr(colon + 1, name.size() - colon - 1);
        } else {
            /* 0 or 2+ colons.  Bare hostname or IPv6 litearal. */
            *host = name;
            *port = std::string();
        }
    }
    return true;
}

connx_error connx_blocking_resolve_address(const char* name, const char* default_port,
                                           connx_resolved_addresses** addresses) {

    struct addrinfo hints;
    struct addrinfo *result = nullptr, *resp;
    int s;
    size_t i;
    connx_error err = CONNX_ERROR_NONE;

    /* parse name, splitting it into host and port parts */
    std::string host;
    std::string port;
    connx_split_host_port(name, &host, &port);

    if (host.empty()) {
        err = CONNX_ERROR_FROM_FORMAT("unparseable host:port (%s)", name);
        goto done;
    }

    if (port.empty()) {
        if (default_port == nullptr) {
            err = CONNX_ERROR_FROM_FORMAT("no port in name (%s)", name);
            goto done;
        }
        port.assign(default_port);
    }

    /* Call getaddrinfo */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     /* ipv4 or ipv6 */
    hints.ai_socktype = SOCK_STREAM; /* stream socket */
    hints.ai_flags = 0;

    s = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
    if (s != 0) {
        /* Retry if well-known service name is recognized */
        const char* svc[][2] = {{"http", "80"}, {"https", "443"}};
        for (i = 0; i < CONNX_ARRAY_SIZE(svc); i++) {
            if (port == svc[i][0]) {
                s = getaddrinfo(host.c_str(), svc[i][1], &hints, &result);
                break;
            }
        }
    }

    if (s != 0) {
#ifdef _WIN32
        err = CONNX_SYSTEM_ERROR(WSAGetLastError(), "getaddrinfo");
#else
        err = connx_getaddrinfo_error(s);
#endif
        goto done;
    }

    /* Success path: set addrs non-NULL, fill it in */
    *addresses = static_cast<connx_resolved_addresses*>(malloc(sizeof(connx_resolved_addresses)));
    (*addresses)->naddrs = 0;
    for (resp = result; resp != nullptr; resp = resp->ai_next) {
        (*addresses)->naddrs++;
    }
    (*addresses)->addrs = static_cast<connx_resolved_address*>(
        malloc(sizeof(connx_resolved_address) * (*addresses)->naddrs));

    i = 0;
    for (resp = result; resp != nullptr; resp = resp->ai_next) {
        memcpy(&(*addresses)->addrs[i].addr, resp->ai_addr, resp->ai_addrlen);
        (*addresses)->addrs[i].len = resp->ai_addrlen;
        i++;
    }
    err = CONNX_ERROR_NONE;
done:
    if (result) {
        freeaddrinfo(result);
    }
    return err;
}

void connx_resolved_addresses_destroy(connx_resolved_addresses* addrs) {
    if (addrs != nullptr) {
        free(addrs->addrs);
    }
    free(addrs);
}
