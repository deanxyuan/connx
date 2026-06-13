/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_NET_TIMER_QUEUE_H_
#define CONNX_SRC_NET_TIMER_QUEUE_H_

#include <stdint.h>
#include <mutex>
#include <queue>
#include <vector>

#include "src/net/connection_id.h"

namespace connx {

enum class TimerKind { kConnectTimeout };

struct TimerEvent {
    int64_t deadline_ms;
    ConnectionId id;
    TimerKind kind;
};

class TimerQueue {
public:
    void Add(int64_t deadline_ms, ConnectionId id, TimerKind kind);
    int NextTimeoutMs(int64_t now_ms, int max_timeout_ms);
    void PopExpired(int64_t now_ms, std::vector<TimerEvent>* out);
    void Clear();

private:
    struct Compare {
        bool operator()(const TimerEvent& lhs, const TimerEvent& rhs) const {
            return lhs.deadline_ms > rhs.deadline_ms;
        }
    };

    std::mutex mtx_;
    std::priority_queue<TimerEvent, std::vector<TimerEvent>, Compare> timers_;
};

} // namespace connx

#endif // CONNX_SRC_NET_TIMER_QUEUE_H_
