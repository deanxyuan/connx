/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "connx/client.h"
#include "connx/codec/delimiter_codec.h"
#include "connx/options.h"
#include "test/support/echoserver.h"
#include "test/util/testutil.h"

namespace {

// Helper handler that captures events with condition-variable synchronization,
// so tests can block until the expected callback fires.
class SyncHandler : public connx::ClientHandler {
public:
    void OnConnected() override {
        std::lock_guard<std::mutex> lk(mtx_);
        connected_ = true;
        cv_.notify_one();
    }
    void OnConnectFailed(const char* reason) override {
        std::lock_guard<std::mutex> lk(mtx_);
        connect_failed_ = true;
        fail_reason_ = reason ? reason : "";
        cv_.notify_one();
    }
    void OnClosed() override {
        std::lock_guard<std::mutex> lk(mtx_);
        closed_ = true;
        cv_.notify_one();
    }
    void OnMessage(const void* data, size_t len) override {
        std::lock_guard<std::mutex> lk(mtx_);
        received_.append(static_cast<const char*>(data), len);
        msg_count_++;
        cv_.notify_one();
    }

    bool WaitForConnect(int timeout_ms = 5000) {
        std::unique_lock<std::mutex> lk(mtx_);
        return cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                            [this] { return connected_ || connect_failed_; });
    }
    bool WaitForMessage(int count, int timeout_ms = 5000) {
        std::unique_lock<std::mutex> lk(mtx_);
        return cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                            [this, count] { return msg_count_ >= count; });
    }
    bool WaitForClosed(int timeout_ms = 5000) {
        std::unique_lock<std::mutex> lk(mtx_);
        return cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                            [this] { return closed_; });
    }

    bool connected_ = false;
    bool connect_failed_ = false;
    bool closed_ = false;
    std::string fail_reason_;
    std::string received_;
    int msg_count_ = 0;
    std::mutex mtx_;
    std::condition_variable cv_;
};

} // namespace

// ============================================================================
// 1. Basic echo - connect, send, receive, disconnect
// ============================================================================
TEST(IntegrationTest, echo_send_and_receive) {
    int port = test::StartEchoServer(0);
    ASSERT_TRUE(port > 0);

    SyncHandler handler;
    connx::ClientOptions opts;
    opts.codec = new connx::DelimiterCodec('\n');

    connx::Client* cli = connx::CreateClient(&handler, opts);
    ASSERT_TRUE(cli != nullptr);

    std::string addr = "127.0.0.1:" + std::to_string(port);
    ASSERT_TRUE(cli->Connect(addr.c_str()));

    ASSERT_TRUE(handler.WaitForConnect());
    ASSERT_TRUE(handler.connected_);

    // Send framed data. The delimiter codec includes the '\n' in the consumed
    // frame, so OnMessage receives the full payload including the delimiter.
    const char* msg = "hello from integration test\n";
    ASSERT_TRUE(cli->SendBuffer(msg, strlen(msg)));

    ASSERT_TRUE(handler.WaitForMessage(1));
    ASSERT_EQ(handler.received_, "hello from integration test\n");

    cli->Disconnect();
    connx::ReleaseClient(cli); // deletes codec via ~ClientImpl
    test::StopEchoServer();
}

// ============================================================================
// 2. Connect timeout
// ============================================================================
TEST(IntegrationTest, connect_timeout) {
    SyncHandler handler;
    connx::ClientOptions opts;
    opts.codec = new connx::DelimiterCodec('\n');
    opts.tcp.connect_timeout_ms = 200;

    connx::Client* cli = connx::CreateClient(&handler, opts);
    ASSERT_TRUE(cli != nullptr);

    // TEST-NET-1 (RFC 5737) - non-routable, will time out.
    ASSERT_TRUE(cli->Connect("192.0.2.1:80"));

    ASSERT_TRUE(handler.WaitForConnect(3000));
    ASSERT_TRUE(handler.connect_failed_);

    connx::ReleaseClient(cli);
}

// ============================================================================
// 3. Multiple concurrent clients
// ============================================================================
TEST(IntegrationTest, multiple_clients) {
    int port = test::StartEchoServer(0);
    ASSERT_TRUE(port > 0);

    const int kNumClients = 3;
    SyncHandler handlers[kNumClients];
    connx::Client* clients[kNumClients];

    std::string addr = "127.0.0.1:" + std::to_string(port);

    // Connect all — each client owns its own codec.
    for (int i = 0; i < kNumClients; i++) {
        connx::ClientOptions opts;
        opts.codec = new connx::DelimiterCodec('\n');

        clients[i] = connx::CreateClient(&handlers[i], opts);
        ASSERT_TRUE(clients[i] != nullptr);
        ASSERT_TRUE(clients[i]->Connect(addr.c_str()));
    }

    // Wait for all connections.
    for (int i = 0; i < kNumClients; i++) {
        ASSERT_TRUE(handlers[i].WaitForConnect());
        ASSERT_TRUE(handlers[i].connected_);
    }

    // Each sends a unique message.
    const char* msgs[kNumClients] = {"client-0\n", "client-1\n", "client-2\n"};
    for (int i = 0; i < kNumClients; i++) {
        ASSERT_TRUE(clients[i]->SendBuffer(msgs[i], strlen(msgs[i])));
    }

    // Each receives its own echo.
    for (int i = 0; i < kNumClients; i++) {
        ASSERT_TRUE(handlers[i].WaitForMessage(1));
        ASSERT_EQ(handlers[i].received_, msgs[i]);
    }

    // Disconnect all.
    for (int i = 0; i < kNumClients; i++) {
        clients[i]->Disconnect();
        connx::ReleaseClient(clients[i]);
    }

    test::StopEchoServer();
}

RUN_ALL_TESTS()
