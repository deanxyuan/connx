/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_INCLUDE_CLIENT_H_
#define CONNX_INCLUDE_CLIENT_H_

#include "connx/export.h"
#include "connx/options.h"

namespace connx {

enum class ConnectionState {
    kDisconnected = 0,
    kConnected = 1,
};

struct Metrics {
    // --- Connection State ---
    ConnectionState state = ConnectionState::kDisconnected;

    // --- Traffic Statistics ---
    uint64_t bytes_sent = 0;
    uint64_t bytes_received = 0;

    // --- Performance ---
    size_t pending_bytes = 0; // Bytes waiting to be sent
};

// Interface for handling connection events
class CONNX_API ClientHandler {
public:
    virtual ~ClientHandler();

    // Triggered when connection state changes (connected, disconnected, error)
    virtual void OnConnected() = 0;
    virtual void OnConnectFailed(const char* reason) = 0;
    virtual void OnClosed() = 0;
    virtual void OnMessage(const void* data, size_t len) = 0;
};

// Main Client interface
class CONNX_API Client {
public:
    virtual ~Client();

    // --- Lifecycle ---
    virtual bool Connect(const char* hosts) = 0;
    virtual void Disconnect() = 0;

    virtual bool IsConnected() const = 0;

    // --- Data Interaction ---
    virtual int64_t SendBuffer(const void* data, size_t len) = 0;

    // --- Observability ---
    virtual void GetMetrics(Metrics* out_metrics) const = 0;
};

// Factory function to create a Client instance
CONNX_API Client* CreateClient(ClientHandler* handler, const ClientOptions& opts);
CONNX_API void ReleaseClient(Client* cli);

// --- Library Lifecycle ---
CONNX_API void LibraryInit();
CONNX_API void LibraryShutdown();

} // namespace connx
#endif // CONNX_INCLUDE_CLIENT_H_
