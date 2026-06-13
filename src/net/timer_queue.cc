/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "src/net/timer_queue.h"

namespace connx {

void TimerQueue::Add(int64_t deadline_ms, ConnectionId id, TimerKind kind) {
    if (!id.valid()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mtx_);
    TimerEvent ev;
    ev.deadline_ms = deadline_ms;
    ev.id = id;
    ev.kind = kind;
    timers_.push(ev);
}

int TimerQueue::NextTimeoutMs(int64_t now_ms, int max_timeout_ms) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (timers_.empty()) {
        return max_timeout_ms;
    }
    int64_t diff = timers_.top().deadline_ms - now_ms;
    if (diff <= 0) {
        return 0;
    }
    if (diff > max_timeout_ms) {
        return max_timeout_ms;
    }
    return static_cast<int>(diff);
}

void TimerQueue::PopExpired(int64_t now_ms, std::vector<TimerEvent>* out) {
    if (out == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(mtx_);
    while (!timers_.empty() && timers_.top().deadline_ms <= now_ms) {
        out->push_back(timers_.top());
        timers_.pop();
    }
}

void TimerQueue::Clear() {
    std::lock_guard<std::mutex> lock(mtx_);
    while (!timers_.empty()) {
        timers_.pop();
    }
}

} // namespace connx
