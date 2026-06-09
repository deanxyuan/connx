/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "src/net/connect_timeout.h"

#include <algorithm>

#ifdef _WIN32
#    include <winsock2.h>
#endif

#include "src/utils/time.h"

namespace connx {

void ConnectTimeoutList::Register(SessionId session_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    pending_.push_back(session_id);
    count_.store(static_cast<int>(pending_.size()), std::memory_order_release);
}
void ConnectTimeoutList::Unregister(SessionId session_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    pending_.erase(std::remove(pending_.begin(), pending_.end(), session_id), pending_.end());
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
            SessionId session_id = pending_[read];
            ClientImpl* c = GetSessionRegistry().Acquire(session_id);
            if (c == nullptr) {
                continue;
            }
            bool keep = false;
            bool timeout_hit = false;
            if (c->session_id_.load(std::memory_order_acquire) == session_id &&
                c->state_.load(std::memory_order_acquire) == ConnState::kConnecting) {
                if (now >= c->connect_deadline_) {
                    timeout_hit = true;
                    timed_out.push_back(c);
                } else {
                    keep = true;
                }
            }
            if (keep) {
                pending_[write++] = session_id;
                c->Unref();
            } else if (!timeout_hit) {
                c->Unref();
            }
        }
        pending_.resize(write);
        count_.store(static_cast<int>(write), std::memory_order_release);
    }
    for (auto c : timed_out) {
#ifdef _WIN32
        c->OnErrorEvent(0, WSAETIMEDOUT);
#else
        c->OnErrorEvent(0, ETIMEDOUT);
#endif
        c->Unref();
    }
}

} // namespace connx
