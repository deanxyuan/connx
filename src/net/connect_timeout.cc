/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "src/net/connect_timeout.h"

#include <algorithm>

#include "src/net/clientimpl.h"
#include "src/utils/time.h"

namespace connx {

void ConnectTimeoutList::Register(ClientImpl* impl) {
    std::lock_guard<std::mutex> lock(mtx_);
    pending_.push_back(impl);
    count_.store(static_cast<int>(pending_.size()), std::memory_order_release);
}
void ConnectTimeoutList::Unregister(ClientImpl* impl) {
    std::lock_guard<std::mutex> lock(mtx_);
    pending_.erase(std::remove(pending_.begin(), pending_.end(), impl), pending_.end());
    count_.store(static_cast<int>(pending_.size()), std::memory_order_release);
}

void ConnectTimeoutList::CheckTimeouts() {
    if (count_.load(std::memory_order_acquire) == 0) return;

    std::vector<ClientImpl*> timed_out;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (pending_.empty()) return;

        int64_t now = GetCurrentMillisec();
        size_t write = 0;
        for (size_t read = 0; read < pending_.size(); ++read) {
            ClientImpl* c = pending_[read];
            if (now >= c->connect_deadline_) {
                c->Ref();
                timed_out.push_back(c);
            } else {
                pending_[write++] = c;
            }
        }
        pending_.resize(write);
        count_.store(static_cast<int>(write), std::memory_order_release);
    }
    for (auto c : timed_out) {
        c->OnErrorEvent(0, ETIMEDOUT);
        c->Unref();
    }
}

} // namespace connx
