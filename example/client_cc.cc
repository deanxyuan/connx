/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 *
 * Echo client using connx C API with newline-delimited protocol.
 * Usage: client_c <host:port>
 * Example:
 *  ./server 9000       (in one terminal)
 *  ./client_cc 127.0.0.1:9000  (in another terminal)
 */
#include <connx/client.h>
#include <connx/codec/delimiter_codec.h>
#include <connx/options.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <unistd.h>
#endif

static void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

class EchoClient : public connx::ClientHandler {

private:
    connx::Client* client_;
    bool done_;
    int count_;

public:
    EchoClient()
        : client_(nullptr)
        , done_(false)
        , count_(0) {}

    void Run(const char* host) {
        connx::ClientOptions opts;
        opts.codec = new connx::DelimiterCodec('\n');
        opts.tcp.connect_timeout_ms = 5000;

        client_ = connx::CreateClient(this, opts);
        if (!client_) {
            fprintf(stderr, "failed to create client\n");
            delete opts.codec; // CreateClient failed, caller still owns codec
            return;
        }
        // CreateClient succeeded -- codec ownership transferred to client.
        printf("[info] connecting to %s ...\n", host);
        if (!client_->Connect(host)) {
            fprintf(stderr, "connect failed immediately\n");
            connx::ReleaseClient(client_); // deletes codec via ~ClientImpl
            return;
        }
        while (!done_) {
            sleep_ms(100);
        }

        connx::Metrics m;
        client_->GetMetrics(&m);
        printf("[metrics] sent=%llu recv=%llu\n", (unsigned long long)m.bytes_sent,
               (unsigned long long)m.bytes_received);
        connx::ReleaseClient(client_); // deletes codec via ~ClientImpl
    }

private:
    void OnConnected() override {
        printf("[event] connected\n");
        Send("hello from connx (C++ API)\n");
    }

    void OnConnectFailed(const char* reason) override {
        printf("[event] connect failed: %s\n", reason ? reason : "unknown");
        done_ = true;
    }

    void OnClosed() override {
        printf("[event] connection closed\n");
        done_ = true;
    }

    void OnMessage(const void* data, size_t len) override {
        printf("[recv] %.*s", (int)len, (const char*)data);
        if (++count_ < 3) {
            char buf[128];
            int n = snprintf(buf, sizeof(buf), "echo #%d\n", count_);
            Send(buf, (size_t)n);
        } else {
            printf("[info] disconnecting after %d exchanges\n", count_);
            client_->Disconnect();
            done_ = true;
        }
    }
    void Send(const char* msg) { Send(msg, strlen(msg)); }
    void Send(const void* data, size_t len) {
        if (client_->SendBuffer(data, len)) {
            printf("[send] %.*s", (int)len, (const char*)data);
        } else {
            printf("[send] failed\n");
        };
    }
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <host:port>\n", argv[0]);
        return 1;
    }
    EchoClient client;
    client.Run(argv[1]);
    return 0;
}
