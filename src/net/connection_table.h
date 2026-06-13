/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_NET_CONNECTION_TABLE_H_
#define CONNX_SRC_NET_CONNECTION_TABLE_H_

#include <memory>
#include <mutex>
#include <vector>

#include "src/net/connection_id.h"

namespace connx {

class ClientConnection;

class ConnectionTable {
public:
    ConnectionId Register(const std::shared_ptr<ClientConnection>& conn);
    std::shared_ptr<ClientConnection> Acquire(ConnectionId id);
    void Unregister(ConnectionId id);
    void Clear();

private:
    struct Entry {
        Entry()
            : generation(0)
            , active(false) {}
        uint32_t generation;
        bool active;
        std::weak_ptr<ClientConnection> conn;
    };

    std::mutex mtx_;
    std::vector<Entry> entries_;
    std::vector<uint32_t> free_slots_;
};

} // namespace connx

#endif // CONNX_SRC_NET_CONNECTION_TABLE_H_
