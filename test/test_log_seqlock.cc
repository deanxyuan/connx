/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "test/util/testutil.h"
#include "connx/c.h"
#include "src/utils/log.h"

#include <atomic>
#include <thread>

RUN_ALL_TESTS();

// ---- helpers ----

static std::atomic<int> g_last_level{-1};
static std::atomic<void*> g_last_userdata{nullptr};

static void LogCbCapture(int level, int, unsigned long, const char*, void* ud) {
    g_last_level.store(level, std::memory_order_relaxed);
    g_last_userdata.store(ud, std::memory_order_relaxed);
}

static void ResetCapture() {
    g_last_level.store(-1, std::memory_order_relaxed);
    g_last_userdata.store(nullptr, std::memory_order_relaxed);
}

static void* const UD_A = (void*)0xA0;
static void* const UD_B = (void*)0xB0;

// Each callback verifies that the userdata matches its own sentinel.
// Mismatch means torn read -- abort immediately.
static void LogCbVerifyA(int, int, unsigned long, const char*, void* ud) {
    if (ud != UD_A) {
        fprintf(stderr, "TORN READ: LogCbA got ud=%p, expected %p\n", ud, UD_A);
        abort();
    }
}
static void LogCbVerifyB(int, int, unsigned long, const char*, void* ud) {
    if (ud != UD_B) {
        fprintf(stderr, "TORN READ: LogCbB got ud=%p, expected %p\n", ud, UD_B);
        abort();
    }
}

// ---- 1. Basic read/write correctness ----

TEST(LogSeqlockTest, set_callback_basic) {
    connx_log_set_callback(LogCbCapture, (void*)0xAAAA);
    connx_log_set_min_level(CONNX_LOG_LEVEL_TRACE);

    ResetCapture();
    connx_log(CONNX_LOG_LEVEL_INFO, __LINE__, "test");

    ASSERT_EQ(g_last_level.load(), CONNX_LOG_LEVEL_INFO);
    ASSERT_EQ(g_last_userdata.load(), (void*)0xAAAA);
}

// ---- 2. Default callback works before any user set_callback ----

TEST(LogSeqlockTest, default_callback_before_set) {
    // Reset to default.
    connx_log_set_callback(nullptr, nullptr);
    connx_log_set_min_level(CONNX_LOG_LEVEL_TRACE);

    // Should not crash -- default callback writes to stderr.
    connx_log(CONNX_LOG_LEVEL_INFO, __LINE__, "default callback test");
    // No assertion needed; reaching here without crash = pass.
}

// ---- 3. Sequential updates -- each new pair is visible ----

TEST(LogSeqlockTest, sequential_updates) {
    connx_log_set_min_level(CONNX_LOG_LEVEL_TRACE);

    struct {
        connx_log_callback_t cb;
        void* ud;
    } pairs[] = {
        {LogCbCapture, (void*)0x1000},
        {LogCbCapture, (void*)0x2000},
        {LogCbCapture, (void*)0x3000},
    };

    for (auto& p : pairs) {
        connx_log_set_callback(p.cb, p.ud);
        ResetCapture();
        connx_log(CONNX_LOG_LEVEL_INFO, __LINE__, "seq");
        ASSERT_EQ(g_last_userdata.load(), p.ud);
    }
}

// ---- 4. Concurrent consistency -- single writer, multiple readers ----
//
// Writer toggles between (LogCbVerifyA, UD_A) and (LogCbVerifyB, UD_B).
// Each reader calls connx_log and the callback verifies the pair matches.
// If any torn read occurs, the callback abort()s immediately.

TEST(LogSeqlockTest, concurrent_single_writer_consistency) {
    // Set a valid pair before starting readers so they never see the canary.
    connx_log_set_callback(LogCbVerifyA, UD_A);
    connx_log_set_min_level(CONNX_LOG_LEVEL_TRACE);

    std::atomic<bool> stop{false};
    std::atomic<int> read_count{0};

    const int kReaders = 4;
    std::thread readers[kReaders];
    for (int r = 0; r < kReaders; r++) {
        readers[r] = std::thread([&]() {
            while (!stop.load(std::memory_order_acquire)) {
                connx_log(CONNX_LOG_LEVEL_INFO, __LINE__, "check");
                read_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::thread writer([&]() {
        for (int i = 0; i < 100000; i++) {
            connx_log_set_callback(LogCbVerifyA, UD_A);
            connx_log_set_callback(LogCbVerifyB, UD_B);
        }
        stop.store(true, std::memory_order_release);
    });

    writer.join();
    for (auto& t : readers) {
        t.join();
    }

    ASSERT_TRUE(read_count.load() > 0);
}

// ---- 5. Callback invoked with NULL ----

TEST(LogSeqlockTest, set_callback_null) {
    connx_log_set_callback(nullptr, (void*)0xDEAD);
    connx_log_set_min_level(CONNX_LOG_LEVEL_TRACE);

    // connx_log checks fn != NULL before calling; should not crash.
    connx_log(CONNX_LOG_LEVEL_INFO, __LINE__, "null callback");
    // Reaching here = pass.
}

// ---- 7. min_level filtering still works after seqlock changes ----

TEST(LogSeqlockTest, min_level_filter) {
    connx_log_set_callback(LogCbCapture, (void*)0x9999);
    connx_log_set_min_level(CONNX_LOG_LEVEL_ERROR);

    ResetCapture();

    // Below min_level -- should be filtered out.
    connx_log(CONNX_LOG_LEVEL_DEBUG, __LINE__, "should be filtered");
    ASSERT_EQ(g_last_level.load(), -1);

    // At min_level -- should go through.
    connx_log(CONNX_LOG_LEVEL_ERROR, __LINE__, "should pass");
    ASSERT_EQ(g_last_level.load(), CONNX_LOG_LEVEL_ERROR);
    ASSERT_EQ(g_last_userdata.load(), (void*)0x9999);

    // Restore.
    connx_log_set_min_level(CONNX_LOG_LEVEL_DEBUG);
}
