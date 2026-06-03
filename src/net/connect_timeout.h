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

#include "src/net/clientimpl.h"

namespace connx {

class ConnectTimeoutList {
private:
    std::mutex mtx_;
    std::vector<SessionId> pending_;
    std::atomic<int> count_{0};

public:
    void Register(SessionId session_id);
    void Unregister(SessionId session_id);
    void CheckTimeouts();
};

} // namespace connx

#endif // CONNX_SRC_NET_CONNECT_TIMEOUT_H_
