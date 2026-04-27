/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "test/testutil.h"
#include "src/utils/sync.h"
#include "src/utils/time.h"

#include <thread>

RUN_ALL_TESTS();

using namespace connx;

// Mutex

TEST(SyncTest, mutex_lock_unlock) {
    Mutex m;
    m.Lock();
    m.Unlock();
}

TEST(SyncTest, mutex_raii) {
    Mutex m;
    {
        MutexLock lock(&m);
    }
}

TEST(SyncTest, mutex_protected_counter) {
    Mutex m;
    int counter = 0;
    const int kThreads = 4;
    const int kIncrements = 10000;
    std::thread threads[kThreads];
    for (int t = 0; t < kThreads; t++) {
        threads[t] = std::thread([&]() {
            for (int i = 0; i < kIncrements; i++) {
                MutexLock lock(&m);
                counter++;
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    ASSERT_EQ(counter, kThreads * kIncrements);
}

// ConditionVariable - signal/wait

TEST(SyncTest, condvar_signal) {
    Mutex m;
    ConditionVariable cv;
    bool flag = false;
    std::thread waiter([&]() {
        MutexLock lock(&m);
        while (!flag) {
            cv.Wait(&m);
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    {
        MutexLock lock(&m);
        flag = true;
    }

    cv.Signal();
    waiter.join();
}
TEST(SyncTest, condvar_broadcast) {
    Mutex m;
    ConditionVariable cv;
    int count = 0;
    const int kWaiters = 4;

    std::thread waiters[kWaiters];
    for (int i = 0; i < kWaiters; i++) {
        waiters[i] = std::thread([&]() {
            MutexLock lock(&m);
            count++;
            cv.Wait(&m);
        });
    };
    // Wait for all waiters to be ready
    while (true) {
        MutexLock lock(&m);
        if (count == kWaiters) break;
    }
    cv.Broadcast();
    for (auto& t : waiters) {
        t.join();
    }
}

// ConditionVariable - timeout
TEST(SyncTest, condvar_timeout_returns_1) {
    Mutex m;
    ConditionVariable cv;
    MutexLock lock(&m);
    int result = cv.Wait(&m, 50); // 50ms timeout
    ASSERT_EQ(result, 1);
    // timeout should return 1
}
TEST(SyncTest, condvar_no_timeout_returns_0) {
    Mutex m;
    ConditionVariable cv;
    bool done = false;
    std::thread signaler([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        {
            MutexLock lock(&m);
            done = true;
        }
        cv.Signal();
    });

    MutexLock lock(&m);
    while (!done) {
        int result = cv.Wait(&m, 500);
        // Should return 0 (signaled), not 1 (timeout)
        if (done) break;
        if (result == 1) {
            // timeout before signal - fail
            ASSERT_TRUE(false);
        }
    }
    signaler.join();
}

TEST(SyncTest, condvar_timeout_rough_accuracy) {
    // Verify CLOCK_MONOTONIC-based timeout is roughly accurate.
    // We use a large enough window to avoid flakineSS.
    Mutex m;
    ConditionVariable cv;
    int64_t start = GetCurrentMillisec();
    {
        MutexLock lock(&m);
        cv.Wait(&m, 200);
    }
    int64_t elapsed = GetCurrentMillisec() - start;
    // Should be at least ~200ms, but allow generousmargin
    ASSERT_TRUE(elapsed >= 150);
    ASSERT_TRUE(elapsed < 500);
}

// ConnxOnceInit
static int g_once_counter = 0;
static void init_once_fn() { g_once_counter++; }

TEST(SyncTest, once_init_single_execution) {
    connx_once_t once = CONNX_ONCE_INIT;
    g_once_counter = 0;

    const int kThreads = 8;
    std::thread threads[kThreads];
    for (int i = 0; i < kThreads; i++) {
        threads[i] = std::thread([&]() { ConnxOnceInit(&once, init_once_fn); });
    }
    for (auto& t : threads) {
        t.join();
    }
    ASSERT_EQ(g_once_counter, 1);
}
