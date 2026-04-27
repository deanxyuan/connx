/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "src/net/clientimpl.h"
#include <assert.h>
#include "src/utils/time.h"
#include "connx/codec.h"
#include "src/utils/log.h"

namespace connx {
void ClientImpl::SetOptions(const ClientOptions& opts) { opt_ = opts; }
void ClientImpl::GetMetrics(Metrics* out_metrics) const {
    if (out_metrics == nullptr) return;
    out_metrics->state =
        is_connected_ ? ConnectionState::kConnected : ConnectionState::kDisconnected;
    out_metrics->bytes_sent = number_of_bytes_sent_;
    out_metrics->bytes_received = number_of_bytes_received_;
    out_metrics->pending_bytes = send_buffer_.GetBufferLength();
}

bool ClientImpl::Connect(const char* hosts) {
    connx_resolved_addresses* addrs = nullptr;
    auto e = connx_blocking_resolve_address(hosts, nullptr, &addrs);
    if (e != CONNX_ERROR_NONE) {
        CONNX_LOG_INFO("failed to parse %s. %s", hosts, e->ToString().c_str());
        return false;
    }
    bool submitted = false;
    for (size_t i = 0; i < addrs->naddrs; i++) {
        e = ConnectImpl(&addrs->addrs[i]);
        if (e == CONNX_ERROR_NONE) {
            submitted = true;
            break;
        }
        CONNX_LOG_INFO("connect failed, %s", e->ToString().c_str());
    }
    connx_resolved_addresses_destroy(addrs);
    return submitted;
}

void ClientImpl::BeforeStartWorkThread() {
    next_package_size_ = 0;
    number_of_bytes_sent_ = 0;
    number_of_bytes_received_ = 0;
    last_send_time_ = GetCurrentMillisec();
    last_recv_time_ = 0;
    msg_count_.Store(0);
    shutdown_ = false;

    try {
        thd_ = std::thread(std::bind(&ClientImpl::WorkThread, this));
    } catch (const std::system_error& e) {
        shutdown_ = true;
        if (service_) {
            auto desc = std::string("failed to create work thread: ") + e.what();
            service_->OnConnectFailed(desc.c_str());
        }
        service_ = nullptr;
    }
}

bool ClientImpl::IsConnected() const { return is_connected_; }
bool ClientImpl::IsConnecting() const {
    return connect_deadline_ != 0 && !is_connected_ && !shutdown_;
}

void ClientImpl::TransferData(const Slice& s) {
    auto obj = new ClientImpl::EventNode;
    obj->ev = NetEvent::kNull;
    obj->data = s;
    PostEventNode(obj);
}
void ClientImpl::OnConnected() {
    ClearConnectDeadline(this);
    is_connected_ = true;
    auto obj = new ClientImpl::EventNode;
    obj->ev = NetEvent::kConnectSuccess;
    PostEventNode(obj);
}

void ClientImpl::ParsingProtocol() {

    size_t cache_size = recv_buffer_.GetBufferLength();

    if (cache_size >= next_package_size_ && next_package_size_ > 0) {
        Slice package = recv_buffer_.GetHeader(next_package_size_);
        service_->OnMessage(package.data(), package.size());
        recv_buffer_.MoveHeader(next_package_size_);
        cache_size -= next_package_size_;
        next_package_size_ = 0;
    }

    size_t read_count;
    size_t pack_len;

    Slice packet;
    DecodeResult result;

    while (cache_size > 0) {

        read_count = 1;
        pack_len = 0;

        do {
            packet = recv_buffer_.MergeFront(read_count);
            result = opt_.codec->Decode(packet.data(), packet.size(), &pack_len);
            if (result == DecodeResult::kError) {
                recv_buffer_.ClearBuffer();
                OnErrorEvent(0, 0);
                return;
            }
            if (result == DecodeResult::kNeedMoreData) {
                // reach the end of buffer
                if (read_count >= recv_buffer_.SliceCount()) {
                    return;
                }
                read_count++;
                continue;
            }

            if (cache_size < static_cast<size_t>(pack_len)) {
                next_package_size_ = pack_len;
                return;
            } else {
                // We got a whole packet
                break;
            }

        } while (true);

        if (packet.size() < static_cast<size_t>(pack_len)) {
            packet = recv_buffer_.GetHeader(pack_len);
        } else {
            packet.RemoveTail(packet.size() - pack_len);
        }
        service_->OnMessage(packet.data(), packet.size());
        recv_buffer_.MoveHeader(pack_len);

        cache_size = recv_buffer_.GetBufferLength();
    }
}

void ClientImpl::WorkThread() {
    while (!shutdown_) {
        if (msg_count_.Load() < 1) {
            std::unique_lock<std::mutex> lk(cmtx_);
            cv_.wait_for(lk, std::chrono::milliseconds(300),
                         [this] { return msg_count_.Load() > 0 || shutdown_; });
        }

        auto node = reinterpret_cast<ClientImpl::EventNode*>(mpscq_.pop());
        if (node != nullptr) {
            msg_count_.FetchSub(1, MemoryOrder::RELAXED);
            OnDispatch(node);
            delete node;
        }
    }
}

void ClientImpl::OnDispatch(ClientImpl::EventNode* node) {
    switch (node->ev) {
    case NetEvent::kNull: {
        recv_buffer_.AddSlice(node->data);
        if (recv_buffer_.GetBufferLength() >= next_package_size_) {
            ParsingProtocol();
        }
        break;
    }
    case NetEvent::kConnectFailed:
        service_->OnConnectFailed(node->desc.c_str());
        break;
    case NetEvent::kConnectSuccess:
        ClearConnectDeadline(this); // double check
        service_->OnConnected();
        break;
    case NetEvent::kClosed:
        service_->OnClosed();
        break;
    }
}
} // namespace connx
