/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "src/net/runtime.h"

#include <algorithm>

#include "src/net/client_connection.h"
#include "src/utils/log.h"
#include "src/utils/time.h"

#include <inttypes.h>

namespace connx {
namespace {

constexpr size_t kDefaultWorkerThreads = 2;
constexpr size_t kDefaultMaxWorkerThreads = 8;

size_t DefaultWorkerThreads(unsigned int hardware_concurrency) {
    if (hardware_concurrency == 0) {
        return 2;
    }
    if (hardware_concurrency <= 2) {
        return 1;
    }

    size_t worker_count = static_cast<size_t>(hardware_concurrency / 2);
    worker_count = std::max<size_t>(2, worker_count);
    return std::min<size_t>(worker_count, kDefaultMaxWorkerThreads);
}

const char* WorkerThreadModeName(int mode) {
    switch (mode) {
    case 0:
        return "default";
    case 1:
        return "auto";
    case 2:
        return "fixed";
    default:
        return "unknown";
    }
}

} // namespace

GlobalRuntime& GlobalRuntime::Instance() {
    static GlobalRuntime runtime;
    return runtime;
}

GlobalRuntime::GlobalRuntime()
    : running_(false)
    , worker_thread_mode_(kWorkerThreadsDefault)
    , configured_worker_threads_(kDefaultWorkerThreads) {}

GlobalRuntime::~GlobalRuntime() { Stop(); }

connx_error GlobalRuntime::Start() {
    if (running_.load(std::memory_order_acquire)) {
        return CONNX_ERROR_NONE;
    }

    connx_error err = poller_.Init();
    if (err != CONNX_ERROR_NONE) {
        CONNX_LOG_ERROR("connx runtime poller init failed: %s", err->ToString().c_str());
        return err;
    }

    unsigned int n = std::thread::hardware_concurrency();
    size_t worker_count = ResolveWorkerThreads(n);
    if (!workers_.Start(worker_count)) {
        CONNX_LOG_ERROR("connx runtime worker pool start failed workers=%" PRIuMAX,
                        static_cast<uintmax_t>(worker_count));
        poller_.Shutdown();
        return CONNX_ERROR_FROM_STATIC_STRING("failed to start worker pool");
    }

    running_.store(true, std::memory_order_release);
    try {
        poll_thread_ = std::thread(&GlobalRuntime::PollLoop, this);
    } catch (...) {
        running_.store(false, std::memory_order_release);
        CONNX_LOG_ERROR("connx runtime poll thread start failed");
        workers_.Stop();
        poller_.Shutdown();
        return CONNX_ERROR_FROM_STATIC_STRING("failed to start poll thread");
    }
    int worker_mode = worker_thread_mode_.load(std::memory_order_acquire);
    CONNX_LOG_DEBUG(
        "connx runtime started workers=%" PRIuMAX " configured_workers=%" PRIuMAX " mode=%s "
        "hardware_concurrency=%u",
        static_cast<uintmax_t>(worker_count), static_cast<uintmax_t>(GetWorkerThreads()),
        WorkerThreadModeName(worker_mode), n);
    return CONNX_ERROR_NONE;
}

void GlobalRuntime::Stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    poller_.Wake();
    if (poll_thread_.joinable()) {
        poll_thread_.join();
    }

    workers_.Stop();
    timers_.Clear();
    connections_.Clear();
    poller_.Shutdown();
    CONNX_LOG_DEBUG("connx runtime stopped");
}

bool GlobalRuntime::SetWorkerThreads(size_t worker_threads) {
    if (running_.load(std::memory_order_acquire)) {
        return false;
    }
    if (worker_threads == 0) {
        configured_worker_threads_.store(kDefaultWorkerThreads, std::memory_order_release);
        worker_thread_mode_.store(kWorkerThreadsDefault, std::memory_order_release);
        return true;
    }
    configured_worker_threads_.store(worker_threads, std::memory_order_release);
    worker_thread_mode_.store(kWorkerThreadsFixed, std::memory_order_release);
    return true;
}

bool GlobalRuntime::SetWorkerThreadsAuto() {
    if (running_.load(std::memory_order_acquire)) {
        return false;
    }
    configured_worker_threads_.store(0, std::memory_order_release);
    worker_thread_mode_.store(kWorkerThreadsAuto, std::memory_order_release);
    return true;
}

size_t GlobalRuntime::GetWorkerThreads() const {
    if (IsWorkerThreadsAuto()) {
        return 0;
    }
    return configured_worker_threads_.load(std::memory_order_acquire);
}

bool GlobalRuntime::IsWorkerThreadsAuto() const {
    return worker_thread_mode_.load(std::memory_order_acquire) == kWorkerThreadsAuto;
}

size_t GlobalRuntime::ResolveWorkerThreads(unsigned int hardware_concurrency) const {
    if (IsWorkerThreadsAuto()) {
        return DefaultWorkerThreads(hardware_concurrency);
    }
    size_t configured = configured_worker_threads_.load(std::memory_order_acquire);
    return configured > 0 ? configured : kDefaultWorkerThreads;
}

void GlobalRuntime::PollLoop() {
    const int kMaxPollTimeoutMs = 1000;
    while (running_.load(std::memory_order_acquire)) {
        int64_t now = GetCurrentMillisec();
        int timeout = timers_.NextTimeoutMs(now, kMaxPollTimeoutMs);

        std::vector<PollEvent> events;
        poller_.Wait(timeout, &events);

        for (size_t i = 0; i < events.size(); ++i) {
            std::shared_ptr<ClientConnection> conn = connections_.Acquire(events[i].id);
            if (conn) {
                conn->PostPollEvent(events[i]);
            }
        }

        now = GetCurrentMillisec();
        std::vector<TimerEvent> timers;
        timers_.PopExpired(now, &timers);
        for (size_t i = 0; i < timers.size(); ++i) {
            std::shared_ptr<ClientConnection> conn = connections_.Acquire(timers[i].id);
            if (conn) {
                conn->PostTimerEvent(timers[i]);
            }
        }
    }
}

} // namespace connx
