/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "test/util/testutil.h"
#include "connx/client.h"
#include "connx/codec/delimiter_codec.h"
#include "connx/options.h"

#include <atomic>

// Codec that tracks how many times it was deleted.
class TrackedCodec : public connx::Codec {
public:
    TrackedCodec(std::atomic<int>* counter, char delimiter = '\n')
        : counter_(counter), delimiter_(delimiter) {
        counter_->fetch_add(1, std::memory_order_relaxed);
    }
    ~TrackedCodec() override {
        counter_->fetch_sub(1, std::memory_order_relaxed);
    }
    connx::DecodeResult Decode(const char*, size_t, size_t*) override {
        return connx::DecodeResult::kNeedMoreData;
    }

private:
    std::atomic<int>* counter_;
    char delimiter_;
};

// Minimal handler for ownership tests.
class NullHandler : public connx::ClientHandler {
public:
    void OnConnected() override {}
    void OnConnectFailed(const char*) override {}
    void OnClosed() override {}
    void OnMessage(const void*, size_t) override {}
};

RUN_ALL_TESTS();

// ============================================================================
// C++ API ownership: CreateClient takes ownership of codec on success.
// ReleaseClient → ~ClientImpl must delete codec exactly once.
// ============================================================================
TEST(CppOwnership, create_client_success_takes_ownership) {
    std::atomic<int> alive{0};
    connx::ClientOptions opts;
    opts.codec = new TrackedCodec(&alive);

    ASSERT_EQ(alive.load(), 1);

    NullHandler handler;
    connx::Client* cli = connx::CreateClient(&handler, opts);
    ASSERT_TRUE(cli != nullptr);

    connx::ReleaseClient(cli); // ~ClientImpl deletes codec
    ASSERT_EQ(alive.load(), 0); // destructor ran
}

// CreateClient failure: caller must delete codec manually.
TEST(CppOwnership, create_client_failure_caller_deletes_codec) {
    std::atomic<int> alive{0};
    connx::ClientOptions opts;
    opts.codec = new TrackedCodec(&alive);
    ASSERT_EQ(alive.load(), 1);

    // Null handler → CreateClient must fail.
    connx::Client* cli = connx::CreateClient(nullptr, opts);
    ASSERT_TRUE(cli == nullptr);

    // Caller still owns codec.
    delete opts.codec;
    ASSERT_EQ(alive.load(), 0);
}

// CreateClient failure with null codec.
TEST(CppOwnership, create_client_failure_null_codec) {
    connx::ClientOptions opts;
    opts.codec = nullptr;

    NullHandler handler;
    connx::Client* cli = connx::CreateClient(&handler, opts);
    ASSERT_TRUE(cli == nullptr);
}

// Verify codec is not leaked: create multiple clients in a loop.
TEST(CppOwnership, repeated_create_release) {
    std::atomic<int> alive{0};

    for (int i = 0; i < 10; i++) {
        connx::ClientOptions opts;
        opts.codec = new TrackedCodec(&alive);
        ASSERT_EQ(alive.load(), 1);

        NullHandler handler;
        connx::Client* cli = connx::CreateClient(&handler, opts);
        ASSERT_TRUE(cli != nullptr);

        connx::ReleaseClient(cli);
        ASSERT_EQ(alive.load(), 0);
    }
}
