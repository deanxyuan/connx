/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_NET_CONNECTION_ID_H_
#define CONNX_SRC_NET_CONNECTION_ID_H_

#include <stdint.h>

namespace connx {

struct ConnectionId {
    uint32_t slot;
    uint32_t generation;

    ConnectionId()
        : slot(0)
        , generation(0) {}
    ConnectionId(uint32_t s, uint32_t g)
        : slot(s)
        , generation(g) {}

    bool valid() const { return generation != 0; }

    uint64_t ToUint64() const {
        return (static_cast<uint64_t>(generation) << 32) | static_cast<uint64_t>(slot);
    }

    static ConnectionId FromUint64(uint64_t value) {
        return ConnectionId(static_cast<uint32_t>(value & 0xFFFFFFFFu),
                            static_cast<uint32_t>(value >> 32));
    }

    bool operator==(const ConnectionId& other) const {
        return slot == other.slot && generation == other.generation;
    }

    bool operator!=(const ConnectionId& other) const { return !(*this == other); }
};

} // namespace connx

#endif // CONNX_SRC_NET_CONNECTION_ID_H_
