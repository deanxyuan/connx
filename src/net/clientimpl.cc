/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "src/net/clientimpl.h"
#include <assert.h>
#include <thread>
#include "src/utils/time.h"
#include "connx/codec.h"
#include "src/utils/log.h"
#include "src/utils/useful.h"

namespace connx {
namespace {
SessionRegistry g_session_registry;
}

SessionRegistry& GetSessionRegistry() { return g_session_registry; }

SessionId SessionRegistry::Register(ClientImpl* impl) {
    std::lock_guard<std::mutex> lock(mtx_);
    SessionId id = next_id_++;
    if (id == 0) {
        id = next_id_++;
    }
    impl->Ref();
    sessions_[id] = impl;
    return id;
}

ClientImpl* SessionRegistry::Acquire(SessionId session_id) {
    if (session_id == 0) return nullptr;
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return nullptr;
    }
    it->second->Ref();
    return it->second;
}

ClientImpl* SessionRegistry::Unregister(SessionId session_id) {
    if (session_id == 0) return nullptr;
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return nullptr;
    }
    ClientImpl* impl = it->second;
    sessions_.erase(it);
    return impl;
}

void ClientImpl::SetOptions(const ClientOptions& opts) { opt_ = opts; }
void ClientImpl::GetMetrics(Metrics* out_metrics) const {
    if (out_metrics == nullptr) return;
    out_metrics->state = state_.load(std::memory_order_acquire) == ConnState::kConnected
                             ? ConnectionState::kConnected
                             : ConnectionState::kDisconnected;
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
    state_.store(ConnState::kIdle, std::memory_order_release);
    session_id_.store(0, std::memory_order_release);
    is_connected_.store(false, std::memory_order_release);
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

bool ClientImpl::IsConnected() const {
    return state_.load(std::memory_order_acquire) == ConnState::kConnected;
}
bool ClientImpl::IsConnecting() const {
    return state_.load(std::memory_order_acquire) == ConnState::kConnecting && !shutdown_;
}

void ClientImpl::TransferData(const Slice& s) {
    auto obj = new ClientImpl::EventNode;
    obj->ev = NetEvent::kNull;
    obj->data = s;
    PostEventNode(obj);
}
void ClientImpl::OnConnected() {
    ConnState expected = ConnState::kConnecting;
    if (!state_.compare_exchange_strong(expected, ConnState::kConnected,
                                        std::memory_order_acq_rel)) {
        return;
    }
    ClearConnectDeadline(this);
    is_connected_.store(true, std::memory_order_release);
    auto obj = new ClientImpl::EventNode;
    obj->ev = NetEvent::kConnectSuccess;
    PostEventNode(obj);
}

bool ClientImpl::TryStartConnect() {
    if (shutdown_) return false;
    ConnState expected = ConnState::kIdle;
    return state_.compare_exchange_strong(expected, ConnState::kConnecting,
                                          std::memory_order_acq_rel);
}

bool ClientImpl::BeginCloseState(bool* was_connected) {
    ConnState previous = state_.exchange(ConnState::kClosing, std::memory_order_acq_rel);
    if (previous == ConnState::kIdle || previous == ConnState::kClosing) {
        return false;
    }
    if (was_connected != nullptr) {
        *was_connected = (previous == ConnState::kConnected);
    }
    ClearConnectDeadline(this);
    is_connected_.store(false, std::memory_order_release);
    return true;
}

ClientImpl* ClientImpl::DetachSession() {
    SessionId session_id = session_id_.exchange(0, std::memory_order_acq_rel);
    return GetSessionRegistry().Unregister(session_id);
}

void ClientImpl::FinishClosedState() {
    is_connected_.store(false, std::memory_order_release);
    state_.store(ConnState::kIdle, std::memory_order_release);
}

void ClientImpl::ParsingProtocol() {

    size_t cache_size = recv_buffer_.GetBufferLength();

    if (cache_size >= next_package_size_ && next_package_size_ > 0) {
        Slice package = recv_buffer_.GetHeader(next_package_size_);
        service_->OnMessage(package.data(), package.size());
        if (CONNX_UNLIKELY(shutdown_)) return;
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
        if (CONNX_UNLIKELY(shutdown_)) return;
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
            Ref();
            OnDispatch(node);
            bool should_exit = shutdown_.load(std::memory_order_acquire);
            delete node;
            if (CONNX_UNLIKELY(should_exit)) {
                ClearPendingEventsAndBuffers();
            }
            Unref();
            if (CONNX_UNLIKELY(should_exit)) {
                return;
            }
        }
    }
}

bool ClientImpl::WakeAndJoinWorkThread() {
    cv_.notify_one();
    if (!thd_.joinable()) {
        return true;
    }
    if (thd_.get_id() == std::this_thread::get_id()) {
        thd_.detach();
        return false;
    }
    thd_.join();
    return true;
}

void ClientImpl::ClearPendingEventsAndBuffers() {
    bool empty = true;
    do {
        auto node = reinterpret_cast<ClientImpl::EventNode*>(mpscq_.PopAndCheckEnd(&empty));
        if (node != nullptr) {
            delete node;
        }
    } while (!empty);
    msg_count_.Store(0);
    recv_buffer_.ClearBuffer();
    send_buffer_.ClearBuffer();
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
