/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_NET_CLIENTIMPL_H_
#define CONNX_SRC_NET_CLIENTIMPL_H_

#include <memory>

#include "connx/client.h"
#include "src/net/client_connection.h"
#include "src/utils/refcounted.h"
#include "src/utils/slice.h"
#include "src/utils/status.h"

namespace connx {

class ClientImpl : public RefCounted<ClientImpl> {
public:
    static connx_error Init();
    static void Shutdown();
    static bool SetRuntimeWorkerThreads(size_t worker_threads);
    static bool SetRuntimeWorkerThreadsAuto();
    static size_t GetRuntimeWorkerThreads();
    static bool IsRuntimeWorkerThreadsAuto();

    ClientImpl();
    ~ClientImpl();

    void SetOptions(const ClientOptions& opts);
    void Start(ClientHandler* handler);
    void Stop();

    bool Connect(const char* hosts);
    void Disconnect();
    bool SendMsg(const Slice& msg);
    bool SendMsg(Slice&& msg);
    bool IsConnected() const;
    bool IsConnecting() const;
    void GetMetrics(Metrics* out_metrics) const;

private:
    ClientOptions opts_;
    ClientHandler* handler_;
    std::shared_ptr<ClientConnection> connection_;
};

} // namespace connx

#endif // CONNX_SRC_NET_CLIENTIMPL_H_
