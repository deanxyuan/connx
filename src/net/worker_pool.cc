/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "src/net/worker_pool.h"

namespace connx {

WorkerPool::WorkerPool()
    : stopping_(true)
    , generation_(0)
    , next_worker_(0)
    , workers_(new WorkerList) {}

WorkerPool::~WorkerPool() { Stop(); }

WorkerPool::WorkerState::WorkerState()
    : accepting(false)
    , active_posts(0)
    , wake_pending(false) {}

WorkerPool::WorkerState::~WorkerState() {}

bool WorkerPool::Start(size_t threads) {
    if (threads == 0) {
        threads = 1;
    }

    std::lock_guard<std::mutex> lock(mtx_);
    if (!stopping_.load(std::memory_order_acquire)) {
        return true;
    }
    stopping_.store(false, std::memory_order_release);
    uint64_t generation = generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
    std::shared_ptr<WorkerList> list(new WorkerList);
    try {
        for (size_t i = 0; i < threads; ++i) {
            std::shared_ptr<WorkerState> worker(new WorkerState);
            worker->accepting.store(true, std::memory_order_release);
            list->workers.push_back(worker);
            worker->thread = std::thread(&WorkerPool::Run, this, worker, generation);
        }
        StoreWorkers(list);
    } catch (...) {
        stopping_.store(true, std::memory_order_release);
        generation_.fetch_add(1, std::memory_order_acq_rel);
        for (size_t i = 0; i < list->workers.size(); ++i) {
            list->workers[i]->accepting.store(false, std::memory_order_release);
            std::lock_guard<std::mutex> worker_lock(list->workers[i]->mtx);
            list->workers[i]->wake_pending = true;
            list->workers[i]->cv.notify_one();
        }
        for (size_t i = 0; i < list->workers.size(); ++i) {
            if (list->workers[i]->thread.joinable()) {
                list->workers[i]->thread.join();
            }
        }
        for (size_t i = 0; i < list->workers.size(); ++i) {
            DrainQueue(list->workers[i].get());
        }
        StoreWorkers(std::shared_ptr<WorkerList>(new WorkerList));
        return false;
    }
    return true;
}

void WorkerPool::Stop() {
    std::shared_ptr<WorkerList> list;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (stopping_.load(std::memory_order_acquire)) {
            return;
        }
        stopping_.store(true, std::memory_order_release);
        generation_.fetch_add(1, std::memory_order_acq_rel);
        list = LoadWorkers();
        StoreWorkers(std::shared_ptr<WorkerList>(new WorkerList));
        for (size_t i = 0; i < list->workers.size(); ++i) {
            list->workers[i]->accepting.store(false, std::memory_order_release);
        }
    }

    std::thread::id self = std::this_thread::get_id();
    for (size_t i = 0; i < list->workers.size(); ++i) {
        {
            std::lock_guard<std::mutex> worker_lock(list->workers[i]->mtx);
            list->workers[i]->wake_pending = true;
        }
        list->workers[i]->cv.notify_one();
    }

    for (size_t i = 0; i < list->workers.size(); ++i) {
        if (!list->workers[i]->thread.joinable()) {
            continue;
        }
        if (list->workers[i]->thread.get_id() == self) {
            list->workers[i]->thread.detach();
        } else {
            list->workers[i]->thread.join();
        }
    }

    for (size_t i = 0; i < list->workers.size(); ++i) {
        std::unique_lock<std::mutex> lock(list->workers[i]->mtx);
        list->workers[i]->cv.wait(lock, [list, i] {
            return list->workers[i]->active_posts.load(std::memory_order_acquire) == 0;
        });
    }

    for (size_t i = 0; i < list->workers.size(); ++i) {
        DrainQueue(list->workers[i].get());
    }
}

bool WorkerPool::Post(const std::function<void()>& task) {
    return Post(PickWorker(), task);
}

bool WorkerPool::Post(size_t worker_index, const std::function<void()>& task) {
    if (!task) {
        return false;
    }
    TaskNode* node = new TaskNode(task);
    if (!PostToWorker(worker_index, node)) {
        delete node;
        return false;
    }
    return true;
}

size_t WorkerPool::PickWorker() {
    size_t count = WorkerCount();
    if (count == 0) {
        return 0;
    }
    return next_worker_.fetch_add(1, std::memory_order_relaxed) % count;
}

size_t WorkerPool::WorkerCount() const {
    std::shared_ptr<WorkerList> list = LoadWorkers();
    return list ? list->workers.size() : 0;
}

bool WorkerPool::PostToWorker(size_t worker_index, TaskNode* node) {
    if (stopping_.load(std::memory_order_acquire)) {
        return false;
    }

    std::shared_ptr<WorkerList> list = LoadWorkers();
    if (!list || list->workers.empty()) {
        return false;
    }
    std::shared_ptr<WorkerState> worker =
        list->workers[worker_index % list->workers.size()];

    worker->active_posts.fetch_add(1, std::memory_order_acq_rel);
    if (!worker->accepting.load(std::memory_order_acquire)) {
        if (worker->active_posts.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::lock_guard<std::mutex> lock(worker->mtx);
            worker->cv.notify_all();
        }
        return false;
    }

    bool first = worker->queue.push(node);
    if (first) {
        {
            std::lock_guard<std::mutex> lock(worker->mtx);
            worker->wake_pending = true;
        }
        worker->cv.notify_one();
    }
    if (worker->active_posts.fetch_sub(1, std::memory_order_acq_rel) == 1 &&
        !worker->accepting.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(worker->mtx);
        worker->cv.notify_all();
    }
    return true;
}

void WorkerPool::Run(const std::shared_ptr<WorkerState>& worker, uint64_t generation) {
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(worker->mtx);
            worker->cv.wait(lock, [worker] { return worker->wake_pending; });
            worker->wake_pending = false;
        }

        for (;;) {
            if (ShouldStop(generation)) {
                return;
            }
            TaskNode* node = PopTask(worker.get());
            if (node == nullptr) {
                break;
            }
            if (ShouldStop(generation)) {
                delete node;
                return;
            }
            std::function<void()> task = std::move(node->task);
            delete node;
            task();
        }

        if (ShouldStop(generation)) {
            return;
        }
    }
}

bool WorkerPool::ShouldStop(uint64_t generation) {
    return generation != generation_.load(std::memory_order_acquire) ||
           stopping_.load(std::memory_order_acquire);
}

WorkerPool::TaskNode* WorkerPool::PopTask(WorkerState* worker) {
    for (;;) {
        bool empty = false;
        TaskNode* node =
            static_cast<TaskNode*>(worker->queue.PopAndCheckEnd(&empty));
        if (node != nullptr || empty) {
            return node;
        }
        std::this_thread::yield();
    }
}

void WorkerPool::DrainQueue(WorkerState* worker) {
    for (;;) {
        TaskNode* node = PopTask(worker);
        if (node == nullptr) {
            return;
        }
        delete node;
    }
}

std::shared_ptr<WorkerPool::WorkerList> WorkerPool::LoadWorkers() const {
    return std::atomic_load_explicit(&workers_, std::memory_order_acquire);
}

void WorkerPool::StoreWorkers(const std::shared_ptr<WorkerList>& workers) {
    std::atomic_store_explicit(&workers_, workers, std::memory_order_release);
}

} // namespace connx
