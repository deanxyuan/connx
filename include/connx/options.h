
/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_INCLUDE_OPTIONS_H_
#define CONNX_INCLUDE_OPTIONS_H_

#include <cstddef>
#include <cstdint>

namespace connx {

class Codec;

struct TcpOptions {
    int tcp_nodelay = 1;      // TCP_NODELAY
    int tcp_quickack = 0;     // linux 3.0+
    int send_buffer_size = 0; // 0 means OS default
    int recv_buffer_size = 0;
    int linger_sec = -1;
    int connect_timeout_ms = 0; // 0 = no timeout
};

struct ClientOptions {

    // --- Protocol ---
    Codec* codec = nullptr;

    // --- Transport ---
    TcpOptions tcp;

    // bind to specific local IP, nullptr = OS auto-select
    const char* local_address = nullptr;
};

} // namespace connx
#endif // CONNX_INCLUDE_OPTIONS_H_
