/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_NET_RESOLVE_ADDRESS_H_
#define CONNX_SRC_NET_RESOLVE_ADDRESS_H_

#include <stddef.h>
#include <stdint.h>
#include <string>

#include "src/utils/status.h"

typedef struct {
    char addr[128];
    size_t len;
} connx_resolved_address;

typedef struct {
    size_t naddrs;
    connx_resolved_address* addrs;
} connx_resolved_addresses;

int connx_join_host_port(char** out, const char* host, int port);
bool connx_split_host_port(const std::string& name, std::string* host, std::string* port);

/* Resolve addr in a blocking fashion. On success,
   default_port can be nullptr, or "https" or "http"
   result must be freed with connx_resolved_addresses_destroy. */
connx_error connx_blocking_resolve_address(const char* name, const char* default_port,
                                           connx_resolved_addresses** addresses);

void connx_resolved_addresses_destroy(connx_resolved_addresses* addrs);

#endif // CONNX_SRC_NET_RESOLVE_ADDRESS_H_
