/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_NET_SOCKET_UTIL_H_
#define CONNX_SRC_NET_SOCKET_UTIL_H_

#include "src/net/resolve_address.h"

/* Writes 0.0.0.0:port. */
void connx_sockaddr_make_wildcard4(int port, connx_resolved_address* resolved_wild_out);

int connx_string_to_sockaddr(connx_resolved_address* out, const char* addr, int port);

/* Set IP port number of a sockaddr */
int connx_sockaddr_set_port(const connx_resolved_address* resolved_addr, int port);

/* Return the IP port number of a sockaddr */
int connx_sockaddr_get_port(const connx_resolved_address* resolved_addr);

#endif // CONNX_SRC_NET_SOCKET_UTIL_H_
