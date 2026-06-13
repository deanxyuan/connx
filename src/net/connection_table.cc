/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "src/net/connection_table.h"

namespace connx {

ConnectionId ConnectionTable::Register(const std::shared_ptr<ClientConnection>& conn) {
    std::lock_guard<std::mutex> lock(mtx_);

    uint32_t slot = 0;
    if (!free_slots_.empty()) {
        slot = free_slots_.back();
        free_slots_.pop_back();
    } else {
        slot = static_cast<uint32_t>(entries_.size());
        entries_.push_back(Entry());
    }

    Entry& entry = entries_[slot];
    entry.generation++;
    if (entry.generation == 0) {
        entry.generation++;
    }
    entry.active = true;
    entry.conn = conn;
    return ConnectionId(slot, entry.generation);
}

std::shared_ptr<ClientConnection> ConnectionTable::Acquire(ConnectionId id) {
    if (!id.valid()) {
        return std::shared_ptr<ClientConnection>();
    }

    std::lock_guard<std::mutex> lock(mtx_);
    if (id.slot >= entries_.size()) {
        return std::shared_ptr<ClientConnection>();
    }

    Entry& entry = entries_[id.slot];
    if (!entry.active || entry.generation != id.generation) {
        return std::shared_ptr<ClientConnection>();
    }
    return entry.conn.lock();
}

void ConnectionTable::Unregister(ConnectionId id) {
    if (!id.valid()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mtx_);
    if (id.slot >= entries_.size()) {
        return;
    }

    Entry& entry = entries_[id.slot];
    if (!entry.active || entry.generation != id.generation) {
        return;
    }

    entry.active = false;
    entry.conn.reset();
    free_slots_.push_back(id.slot);
}

void ConnectionTable::Clear() {
    std::lock_guard<std::mutex> lock(mtx_);
    entries_.clear();
    free_slots_.clear();
}

} // namespace connx
