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
        return cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [this] { return closed_; });
    }

    void ResetMessageState() {
        std::unique_lock<std::mutex> lk(mtx_);
        received_.clear();
        msg_count_ = 0;
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

class ReleaseOnConnectedHandler : public connx::ClientHandler {
private:
    connx::Client** client_;

public:
    explicit ReleaseOnConnectedHandler(connx::Client** client)
        : client_(client) {}

    void OnConnected() override {
        connx::Client* cli = *client_;
        *client_ = nullptr;
        connx::ReleaseClient(cli);
    }
    void OnConnectFailed(const char* d) override {}
    void OnClosed() override {}
    void OnMessage(const void*, size_t) override {}
};
class ReleaseOnMessageHandler : public connx::ClientHandler {
private:
    connx::Client** client_;

public:
    explicit ReleaseOnMessageHandler(connx::Client** client)
        : client_(client) {}

    void OnConnected() override {
        const char* msg = "release-on-message\n";
        (*client_)->SendBuffer(msg, strlen(msg));
    }
    void OnConnectFailed(const char* d) override {}
    void OnClosed() override {}
    void OnMessage(const void*, size_t) override {
        connx::Client* cli = *client_;
        *client_ = nullptr;
        connx::ReleaseClient(cli);
    }
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
    ASSERT_TRUE(cli->Connect("192.0.2.1:6580"));

    ASSERT_TRUE(handler.WaitForConnect(3000));
    ASSERT_TRUE(handler.connect_failed_);

    connx::ReleaseClient(cli);
}

// ============================================================================
// 3. Connection refused should fail without a spurious OnConnected
// ============================================================================
TEST(IntegrationTest, connection_refused_reports_failure_only) {
    SyncHandler handler;
    connx::ClientOptions opts;
    opts.codec = new connx::DelimiterCodec('\n');
    opts.tcp.connect_timeout_ms = 1000;

    connx::Client* cli = connx::CreateClient(&handler, opts);
    ASSERT_TRUE(cli != nullptr);
    // Port 1 on loopback is typically closed and should fail quickly
    ASSERT_TRUE(cli->Connect("127.0.0.1:1"));

    ASSERT_TRUE(handler.WaitForConnect(3000));
    ASSERT_TRUE(!handler.connected_);
    ASSERT_TRUE(handler.connect_failed_);

    connx::ReleaseClient(cli);
}

// ============================================================================
// 4. Multiple concurrent clients
// ============================================================================
TEST(IntegrationTest, multiple_clients) {
    int port = test::StartEchoServer(0);
    ASSERT_TRUE(port > 0);

    const int kNumClients = 3;
    SyncHandler handlers[kNumClients];
    connx::Client* clients[kNumClients];

    std::string addr = "127.0.0.1:" + std::to_string(port);

    // Connect all -- each client owns its own codec.
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

// ============================================================================
// 5. Peer close should surface a single close event
// ============================================================================
TEST(IntegrationTest, peer_close_reports_single_close) {
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

    const char* msg = "close-after-echo\n";
    ASSERT_TRUE(cli->SendBuffer(msg, strlen(msg)));
    ASSERT_TRUE(handler.WaitForMessage(1));
    ASSERT_EQ(handler.received_, "close-after-echo\n");

    handler.ResetMessageState();
    test::StopEchoServer();

    ASSERT_TRUE(handler.WaitForClosed(3000));
    ASSERT_TRUE(handler.closed_);
    ASSERT_TRUE(!handler.connect_failed_);

    connx::ReleaseClient(cli);
}

// ============================================================================
// 6. Releasing from a callback should not self-join or delete the active
//    ClientImpl while its work thread is still dispatching
// ============================================================================
TEST(IntegrationTest, release_from_callback_does_not_crash) {
    int port = test::StartEchoServer(0);
    ASSERT_TRUE(port > 0);

    connx::Client* cli = nullptr;
    ReleaseOnConnectedHandler hander(&cli);
    connx::ClientOptions opts;
    opts.codec = new connx::DelimiterCodec('\n');

    cli = connx::CreateClient(&hander, opts);
    ASSERT_TRUE(cli != nullptr);

    std::string addr = "127.0.0.1:" + std::to_string(port);
    ASSERT_TRUE(cli->Connect(addr.c_str()));

    for (int i = 0; i < 50 && cli != nullptr; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ASSERT_TRUE(cli == nullptr);

    test::StopEchoServer();
}

TEST(IntegrationTest, release_from_message_callback_does_not_crash) {
    int port = test::StartEchoServer(0);
    ASSERT_TRUE(port > 0);

    connx::Client* cli = nullptr;
    ReleaseOnMessageHandler hander(&cli);
    connx::ClientOptions opts;
    opts.codec = new connx::DelimiterCodec('\n');

    cli = connx::CreateClient(&hander, opts);
    ASSERT_TRUE(cli != nullptr);

    std::string addr = "127.0.0.1:" + std::to_string(port);
    ASSERT_TRUE(cli->Connect(addr.c_str()));

    for (int i = 0; i < 50 && cli != nullptr; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ASSERT_TRUE(cli == nullptr);

    test::StopEchoServer();
}
RUN_ALL_TESTS()
