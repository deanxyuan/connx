/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "test/util/testutil.h"
#include "src/utils/refcounted.h"

#include <atomic>
#include <thread>
#include <vector>

RUN_ALL_TESTS();

using namespace connx;

struct Counted : public RefCounted<Counted> {
    static std::atomic<int> alive;
    Counted() { alive.fetch_add(1); }
    ~Counted() override { alive.fetch_sub(1); }
};

std::atomic<int> Counted::alive{0};

// Basic

TEST(RefCountedTest, initial_refcount) {
    Counted::alive.store(0);
    auto* p = new Counted();
    ASSERT_EQ(Counted::alive.load(), 1);
    p->Unref();
    ASSERT_EQ(Counted::alive.load(), 0);
}
TEST(RefCountedTest, ref_increments) {
    Counted::alive.store(0);
    auto* p = new Counted();
    p->Ref();
    p->Ref();
    // refcount is now 3
    p->Unref();
    ASSERT_EQ(Counted::alive.load(), 1);
    p->Unref();
    ASSERT_EQ(Counted::alive.load(), 1);
    p->Unref();
    ASSERT_EQ(Counted::alive.load(), 0);
}

TEST(RefCountedTest, unref_to_zero_deletes) {
    Counted::alive.store(0);
    auto* p = new Counted();
    ASSERT_EQ(Counted::alive.load(), 1);
    p->Unref();
    ASSERT_EQ(Counted::alive.load(), 0);
}

// Ref then multiple Unref

TEST(RefCountedTest, ref_unref_balanced) {
    Counted::alive.store(0);
    auto* p = new Counted();
    for (int i = 0; i < 100; i++) {
        p->Ref();
    }
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ(Counted::alive.load(), 1);
        p->Unref();
    }
    // Still alive (initial ref)
    ASSERT_EQ(Counted::alive.load(), 1);
    p->Unref();
    ASSERT_EQ(Counted::alive.load(), 0);
}

// Multi-thread concurrent Ref/Unref
TEST(RefCountedTest, multi_thread_no_leak) {
    Counted::alive.store(0);
    auto* p = new Counted();

    constexpr int kThreads = 8;
    constexpr int kOps = 10000;
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([kOps, p]() {
            for (int i = 0; i < kOps; i++) {
                p->Ref();
                p->Unref();
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    ASSERT_EQ(Counted::alive.load(), 1);
    p->Unref();
    ASSERT_EQ(Counted::alive.load(), 0);
}

TEST(RefCountedTest, multi_thread_balanced_refs) {
    Counted::alive.store(0);
    auto* p = new Counted();

    constexpr int kThreads = 4;
    constexpr int kRefs = 5000;
    // Each thread adds kRefs references
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([kRefs, p]() {
            for (int i = 0; i < kRefs; i++) {
                p->Ref();
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(Counted::alive.load(), 1);
    // Unref from one thread until fully released
    int total = 1 + kThreads * kRefs;
    for (int i = 0; i < total; i++) {
        p->Unref();
    }
    ASSERT_EQ(Counted::alive.load(), 0);
}
