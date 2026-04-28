/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "connx/client.h"
#include "connx/codec.h"
#include "src/net/clientimpl.h"
#include "src/net/resolve_address.h"
#include <stdlib.h>

namespace connx {
ClientHandler::~ClientHandler() {}
Client::~Client() {}
Codec::~Codec() {}

class ClientAdaptor : public Client {
private:
    ClientImpl* impl_;

public:
    ClientAdaptor() { impl_ = new ClientImpl(); }
    ~ClientAdaptor() {
        impl_->Stop();
        impl_->Unref();
    }

    void Init(ClientHandler* handler, const ClientOptions& opts) {
        impl_->SetOptions(opts);
        impl_->Start(handler);
    }

    bool Connect(const char* hosts) override {
        if (hosts == nullptr) return false;
        return impl_->Connect(hosts);
    }
    bool Connect(const char* ip, int port) override {
        if (ip == nullptr || port <= 0 || port > 65535) return false;
        char* addr = nullptr;
        connx_join_host_port(&addr, ip, port);
        if (addr == nullptr) return false;
        bool ok = impl_->Connect(addr);
        free(addr);
        return ok;
    }

    void Disconnect() override { impl_->Disconnect(); }

    bool IsConnected() const override { return impl_->IsConnected(); }

    // --- Data Interaction ---
    bool SendBuffer(const void* data, size_t size) override {
        if (data == nullptr || size == 0) {
            return false;
        }
        return impl_->SendMsg(Slice(data, size));
    }

    // --- Observability ---
    void GetMetrics(Metrics* metrics) const override { impl_->GetMetrics(metrics); }
};

Client* CreateClient(ClientHandler* handler, const ClientOptions& opts) {
    if (handler == nullptr || opts.codec == nullptr) return nullptr;
    auto p = new ClientAdaptor();
    p->Init(handler, opts);
    return p;
}

void ReleaseClient(Client* cli) {
    if (cli) delete cli;
}
} // namespace connx
