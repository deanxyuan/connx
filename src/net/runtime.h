/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_NET_RUNTIME_H_
#define CONNX_SRC_NET_RUNTIME_H_

#include <atomic>
#include <thread>

#include "src/net/connection_table.h"
#include "src/net/poller.h"
#include "src/net/timer_queue.h"
#include "src/net/worker_pool.h"

namespace connx {

class GlobalRuntime {
public:
    static GlobalRuntime& Instance();

    connx_error Start();
    void Stop();
    bool SetWorkerThreads(size_t worker_threads);
    bool SetWorkerThreadsAuto();
    size_t GetWorkerThreads() const;
    bool IsWorkerThreadsAuto() const;

    Poller& poller() { return poller_; }
    TimerQueue& timers() { return timers_; }
    WorkerPool& workers() { return workers_; }
    ConnectionTable& connections() { return connections_; }

private:
    GlobalRuntime();
    ~GlobalRuntime();

    GlobalRuntime(const GlobalRuntime&) = delete;
    GlobalRuntime& operator=(const GlobalRuntime&) = delete;

    void PollLoop();
    size_t ResolveWorkerThreads(unsigned int hardware_concurrency) const;

    enum WorkerThreadMode {
        kWorkerThreadsDefault = 0,
        kWorkerThreadsAuto = 1,
        kWorkerThreadsFixed = 2,
    };

    std::atomic<bool> running_;
    std::atomic<int> worker_thread_mode_;
    std::atomic<size_t> configured_worker_threads_;
    std::thread poll_thread_;
    Poller poller_;
    TimerQueue timers_;
    WorkerPool workers_;
    ConnectionTable connections_;
};

} // namespace connx

#endif // CONNX_SRC_NET_RUNTIME_H_
