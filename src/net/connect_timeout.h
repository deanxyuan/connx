/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_NET_CONNECT_TIMEOUT_H_
#define CONNX_SRC_NET_CONNECT_TIMEOUT_H_

#include <stdint.h>
#include <atomic>
#include <mutex>
#include <vector>

namespace connx {
class ClientImpl;

class ConnectTimeoutList {
private:
    std::mutex mtx_;
    std::vector<ClientImpl*> pending_;
    std::atomic<int> count_{0};

public:
    void Register(ClientImpl*);
    void Unregister(ClientImpl*);
    void CheckTimeouts();
};

} // namespace connx

#endif // CONNX_SRC_NET_CONNECT_TIMEOUT_H_
