/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_NET_WORKER_POOL_H_
#define CONNX_SRC_NET_WORKER_POOL_H_

#include <stdint.h>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "src/utils/mpscq.h"

namespace connx {

class WorkerPool {
public:
    WorkerPool();
    ~WorkerPool();

    bool Start(size_t threads);
    void Stop();
    bool Post(const std::function<void()>& task);
    bool Post(size_t worker_index, const std::function<void()>& task);
    size_t PickWorker();
    size_t WorkerCount() const;

private:
    struct TaskNode : public MultiProducerSingleConsumerQueue::Node {
        explicit TaskNode(const std::function<void()>& fn)
            : task(fn) {}
        std::function<void()> task;
    };

    struct WorkerState {
        WorkerState();
        ~WorkerState();

        std::mutex mtx;
        std::condition_variable cv;
        MultiProducerSingleConsumerQueue queue;
        std::thread thread;
        std::atomic<bool> accepting;
        std::atomic<size_t> active_posts;
        bool wake_pending;
    };

    struct WorkerList {
        std::vector<std::shared_ptr<WorkerState> > workers;
    };

    void Run(const std::shared_ptr<WorkerState>& worker, uint64_t generation);
    bool PostToWorker(size_t worker_index, TaskNode* node);
    bool ShouldStop(uint64_t generation);
    void DrainQueue(WorkerState* worker);
    TaskNode* PopTask(WorkerState* worker);

    std::shared_ptr<WorkerList> LoadWorkers() const;
    void StoreWorkers(const std::shared_ptr<WorkerList>& workers);

    std::mutex mtx_;
    std::atomic<bool> stopping_;
    std::atomic<uint64_t> generation_;
    std::atomic<size_t> next_worker_;
    std::shared_ptr<WorkerList> workers_;
};

} // namespace connx

#endif // CONNX_SRC_NET_WORKER_POOL_H_
