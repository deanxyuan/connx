/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "src/net/clientimpl.h"

#include "src/net/runtime.h"

namespace connx {

connx_error ClientImpl::Init() { return GlobalRuntime::Instance().Start(); }

void ClientImpl::Shutdown() { GlobalRuntime::Instance().Stop(); }

bool ClientImpl::SetRuntimeWorkerThreads(size_t worker_threads) {
    return GlobalRuntime::Instance().SetWorkerThreads(worker_threads);
}

bool ClientImpl::SetRuntimeWorkerThreadsAuto() {
    return GlobalRuntime::Instance().SetWorkerThreadsAuto();
}

size_t ClientImpl::GetRuntimeWorkerThreads() {
    return GlobalRuntime::Instance().GetWorkerThreads();
}

bool ClientImpl::IsRuntimeWorkerThreadsAuto() {
    return GlobalRuntime::Instance().IsWorkerThreadsAuto();
}

ClientImpl::ClientImpl()
    : opts_()
    , handler_(nullptr) {}

ClientImpl::~ClientImpl() { Stop(); }

void ClientImpl::SetOptions(const ClientOptions& opts) { opts_ = opts; }

void ClientImpl::Start(ClientHandler* handler) {
    handler_ = handler;
    if (!connection_) {
        connection_ = std::make_shared<ClientConnection>(handler_, opts_);
        opts_.codec = nullptr;
    }
}

void ClientImpl::Stop() {
    if (connection_) {
        connection_->CloseFromOwner();
        connection_.reset();
    }
}

bool ClientImpl::Connect(const char* hosts) {
    if (!connection_ || hosts == nullptr) {
        return false;
    }
    return connection_->Connect(hosts);
}

void ClientImpl::Disconnect() {
    if (connection_) {
        connection_->Disconnect();
    }
}

bool ClientImpl::SendMsg(const Slice& msg) {
    if (!connection_ || msg.empty()) {
        return false;
    }
    return connection_->SendBuffer(msg.data(), msg.size());
}

bool ClientImpl::SendMsg(Slice&& msg) {
    if (!connection_ || msg.empty()) {
        return false;
    }
    return connection_->SendBuffer(msg.data(), msg.size());
}

bool ClientImpl::IsConnected() const { return connection_ && connection_->IsConnected(); }

bool ClientImpl::IsConnecting() const { return connection_ && connection_->IsConnecting(); }

void ClientImpl::GetMetrics(Metrics* out_metrics) const {
    if (connection_) {
        connection_->GetMetrics(out_metrics);
    } else if (out_metrics != nullptr) {
        *out_metrics = Metrics();
    }
}

} // namespace connx
