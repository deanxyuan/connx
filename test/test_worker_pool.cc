/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "test/util/testutil.h"
#include "src/net/worker_pool.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

RUN_ALL_TESTS();

using namespace connx;

namespace {

bool WaitUntil(std::mutex* mtx, std::condition_variable* cv, std::atomic<int>* value, int expected,
               int timeout_ms) {
    std::unique_lock<std::mutex> lock(*mtx);
    return cv->wait_for(lock, std::chrono::milliseconds(timeout_ms),
                        [value, expected] { return value->load() >= expected; });
}

} // namespace

TEST(WorkerPoolTest, directed_mpsc_queues_run_all_tasks) {
    WorkerPool pool;
    ASSERT_TRUE(pool.Start(4));
    ASSERT_EQ(pool.WorkerCount(), static_cast<size_t>(4));

    const int kProducers = 8;
    const int kTasksPerProducer = 500;
    const int kTotalTasks = kProducers * kTasksPerProducer;

    std::atomic<int> completed{0};
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<std::thread> producers;

    for (int producer = 0; producer < kProducers; ++producer) {
        producers.push_back(
            std::thread([producer, kTasksPerProducer, kTotalTasks, &pool, &completed, &mtx, &cv]() {
                for (int i = 0; i < kTasksPerProducer; ++i) {
                    size_t worker_index = static_cast<size_t>((producer + i) % 4);
                    bool ok = pool.Post(worker_index, [kTotalTasks, &completed, &mtx, &cv]() {
                        int value = completed.fetch_add(1, std::memory_order_relaxed) + 1;
                        if ((value % 128) == 0 || value == kTotalTasks) {
                            std::lock_guard<std::mutex> lock(mtx);
                            cv.notify_all();
                        }
                    });
                    ASSERT_TRUE(ok);
                }
            }));
    }

    for (size_t i = 0; i < producers.size(); ++i) {
        producers[i].join();
    }

    ASSERT_TRUE(WaitUntil(&mtx, &cv, &completed, kTotalTasks, 5000));
    ASSERT_EQ(completed.load(), kTotalTasks);
    pool.Stop();
}

TEST(WorkerPoolTest, round_robin_post_runs_tasks) {
    WorkerPool pool;
    ASSERT_TRUE(pool.Start(3));

    const int kTasks = 300;
    std::atomic<int> completed{0};
    std::mutex mtx;
    std::condition_variable cv;

    for (int i = 0; i < kTasks; ++i) {
        ASSERT_TRUE(pool.Post([kTasks, &completed, &mtx, &cv]() {
            int value = completed.fetch_add(1, std::memory_order_relaxed) + 1;
            if (value == kTasks) {
                std::lock_guard<std::mutex> lock(mtx);
                cv.notify_all();
            }
        }));
    }

    ASSERT_TRUE(WaitUntil(&mtx, &cv, &completed, kTasks, 5000));
    ASSERT_EQ(completed.load(), kTasks);
    pool.Stop();
}

TEST(WorkerPoolTest, post_after_stop_fails) {
    WorkerPool pool;
    ASSERT_TRUE(pool.Start(2));
    pool.Stop();

    std::atomic<int> completed{0};
    ASSERT_TRUE(!pool.Post([&completed]() { completed.fetch_add(1); }));
    ASSERT_TRUE(!pool.Post(0, [&completed]() { completed.fetch_add(1); }));
    ASSERT_EQ(completed.load(), 0);
}

TEST(WorkerPoolTest, concurrent_post_and_stop_is_safe) {
    WorkerPool pool;
    ASSERT_TRUE(pool.Start(4));

    const int kProducers = 8;
    const int kAttemptsPerProducer = 1000;
    std::atomic<int> attempted{0};
    std::atomic<int> accepted{0};
    std::atomic<int> completed{0};
    std::vector<std::thread> producers;

    for (int producer = 0; producer < kProducers; ++producer) {
        producers.push_back(std::thread(
            [producer, kAttemptsPerProducer, &pool, &attempted, &accepted, &completed]() {
                for (int i = 0; i < kAttemptsPerProducer; ++i) {
                    attempted.fetch_add(1, std::memory_order_relaxed);
                    bool ok = pool.Post(static_cast<size_t>((producer + i) % 4), [&completed]() {
                        completed.fetch_add(1, std::memory_order_relaxed);
                    });
                    if (ok) {
                        accepted.fetch_add(1, std::memory_order_relaxed);
                    }
                    if (i == 50) {
                        std::this_thread::yield();
                    }
                }
            }));
    }

    while (attempted.load(std::memory_order_relaxed) < kProducers * 20) {
        std::this_thread::yield();
    }
    pool.Stop();

    for (size_t i = 0; i < producers.size(); ++i) {
        producers[i].join();
    }

    ASSERT_GE(accepted.load(), completed.load());
    ASSERT_GE(kProducers * kAttemptsPerProducer, attempted.load());
}
