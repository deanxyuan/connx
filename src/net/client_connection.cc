/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "src/net/client_connection.h"

#include "connx/codec.h"
#include "src/net/runtime.h"
#include "src/utils/log.h"
#include "src/utils/string.h"
#include "src/utils/time.h"

#include <inttypes.h>
#include <thread>

namespace connx {
namespace {

thread_local ClientConnection* t_draining_connection = nullptr;

class ScopedDrainMarker {
public:
    explicit ScopedDrainMarker(ClientConnection* conn)
        : previous_(t_draining_connection) {
        t_draining_connection = conn;
    }

    ~ScopedDrainMarker() { t_draining_connection = previous_; }

private:
    ClientConnection* previous_;
};

} // namespace

ClientConnection::ClientConnection(ClientHandler* handler, const ClientOptions& opts)
    : handler_(handler)
    , opts_(opts)
    , state_(State::kIdle)
    , fd_(InvalidSocketHandle())
    , poll_interest_(0)
    , worker_index_(0)
    , connect_deadline_ms_(0)
    , connected_(false)
    , bytes_sent_(0)
    , bytes_received_(0)
    , pending_bytes_(0)
    , accepting_tasks_(true)
    , active_posts_(0)
    , queued_tasks_(0)
    , owner_closing_(false)
    , draining_(false)
#ifdef _WIN32
    , recv_pending_(false)
    , send_pending_(false)
#endif
{
    worker_index_ = GlobalRuntime::Instance().workers().PickWorker();
}

ClientConnection::~ClientConnection() {
    if (id_.valid()) {
        GlobalRuntime::Instance().poller().Remove(fd_);
        GlobalRuntime::Instance().connections().Unregister(id_);
        id_ = ConnectionId();
    }
    CloseFd();
    if (opts_.codec) {
        delete opts_.codec;
        opts_.codec = nullptr;
    }
}

bool ClientConnection::Connect(const char* hosts) {
    if (hosts == nullptr) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(state_mtx_);
        if (state_ != State::kIdle) {
            return false;
        }
        state_ = State::kConnecting;
        connected_.store(false, std::memory_order_release);
    }
    CONNX_LOG_DEBUG("connx client connect requested hosts=%s worker=%" PRIuMAX, hosts,
                    static_cast<uintmax_t>(worker_index_));

    connx_resolved_addresses* addrs = nullptr;
    connx_error err = connx_blocking_resolve_address(hosts, nullptr, &addrs);
    if (err != CONNX_ERROR_NONE || addrs == nullptr || addrs->naddrs == 0) {
        std::string desc = err ? err->ToString() : std::string("resolve failed");
        CONNX_LOG_WARN("connx client resolve failed hosts=%s error=%s", hosts, desc.c_str());
        std::shared_ptr<ClientConnection> self = shared_from_this();
        Post([self, desc] { self->StartClose(CloseReason::kConnectFailed, desc); });
        if (addrs) {
            connx_resolved_addresses_destroy(addrs);
        }
        return false;
    }

    connx_resolved_address addr = addrs->addrs[0];
    connx_resolved_addresses_destroy(addrs);
    std::shared_ptr<ClientConnection> self = shared_from_this();
    Post([self, addr] { self->ConnectResolved(addr); });
    return true;
}

void ClientConnection::Disconnect() {
    std::shared_ptr<ClientConnection> self = shared_from_this();
    Post([self] { self->StartClose(CloseReason::kUser, ""); });
}

bool ClientConnection::SendBuffer(const void* data, size_t len) {
    if (data == nullptr || len == 0) {
        return false;
    }
    if (!IsConnected()) {
        return false;
    }

    Slice msg(data, len);
    std::shared_ptr<ClientConnection> self = shared_from_this();
    Post([self, msg, len] {
        if (!self->IsConnected()) {
            return;
        }
        self->pending_bytes_.fetch_add(len, std::memory_order_relaxed);
        self->send_buffer_.AddSlice(msg);
        self->UpdateWriteInterest();
    });
    return true;
}

bool ClientConnection::IsConnected() const { return connected_.load(std::memory_order_acquire); }

bool ClientConnection::IsConnecting() const {
    std::lock_guard<std::mutex> lock(state_mtx_);
    return state_ == State::kConnecting;
}

void ClientConnection::GetMetrics(Metrics* out_metrics) const {
    if (out_metrics == nullptr) {
        return;
    }
    out_metrics->state =
        IsConnected() ? ConnectionState::kConnected : ConnectionState::kDisconnected;
    out_metrics->bytes_sent = bytes_sent_.load(std::memory_order_relaxed);
    out_metrics->bytes_received = bytes_received_.load(std::memory_order_relaxed);
    out_metrics->pending_bytes = pending_bytes_.load(std::memory_order_relaxed);
}

void ClientConnection::CloseFromOwner() {
    std::shared_ptr<ClientConnection> self = shared_from_this();
    owner_closing_.store(true, std::memory_order_release);
    accepting_tasks_.store(false, std::memory_order_release);

    {
        std::unique_lock<std::mutex> lock(idle_mtx_);
        idle_cv_.wait(lock, [this] { return active_posts_.load(std::memory_order_acquire) == 0; });
    }

    if (t_draining_connection == this) {
        DrainTaskQueue();
        StartClose(CloseReason::kRelease, "");
        return;
    }

    bool expected = false;
    if (draining_.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                          std::memory_order_acquire)) {
        ScheduleDrain(self);
    }

    {
        std::unique_lock<std::mutex> lock(idle_mtx_);
        idle_cv_.wait(lock, [this] {
            return !draining_.load(std::memory_order_acquire) &&
                   queued_tasks_.load(std::memory_order_acquire) == 0 &&
                   active_posts_.load(std::memory_order_acquire) == 0;
        });
    }
}

void ClientConnection::PostPollEvent(const PollEvent& ev) {
    std::shared_ptr<ClientConnection> self = shared_from_this();
    Post([self, ev] { self->HandlePollEvent(ev); });
}

void ClientConnection::PostTimerEvent(const TimerEvent& ev) {
    std::shared_ptr<ClientConnection> self = shared_from_this();
    Post([self, ev] { self->HandleTimerEvent(ev); });
}

void ClientConnection::Post(const std::function<void()>& task) {
    if (!task) {
        return;
    }

    active_posts_.fetch_add(1, std::memory_order_acq_rel);

    TaskNode* node = nullptr;
    try {
        node = new TaskNode(task);
    } catch (...) {
        if (active_posts_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            NotifyIdle();
        }
        throw;
    }

    if (!accepting_tasks_.load(std::memory_order_acquire)) {
        delete node;
        if (active_posts_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            NotifyIdle();
        }
        return;
    }

    queued_tasks_.fetch_add(1, std::memory_order_acq_rel);
    task_queue_.push(node);

    std::shared_ptr<ClientConnection> self;
    bool expected = false;
    if (draining_.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                          std::memory_order_acquire)) {
        self = shared_from_this();
    }

    if (active_posts_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        NotifyIdle();
    }

    if (self) {
        ScheduleDrain(self);
    }
}

void ClientConnection::ScheduleDrain(const std::shared_ptr<ClientConnection>& self) {
    if (!GlobalRuntime::Instance().workers().Post(worker_index_, [self] { self->Drain(); })) {
        CONNX_LOG_WARN("connx worker post failed; drain inline worker=%" PRIuMAX,
                       static_cast<uintmax_t>(worker_index_));
        self->Drain();
    }
}

void ClientConnection::Drain() {
    ScopedDrainMarker marker(this);

    for (;;) {
        if (owner_closing_.load(std::memory_order_acquire)) {
            DrainTaskQueue();
            StartClose(CloseReason::kRelease, "");
            draining_.store(false, std::memory_order_release);
            NotifyIdle();
            return;
        }

        TaskNode* node = PopAvailableTaskNode();
        if (node == nullptr) {
            draining_.store(false, std::memory_order_release);
            NotifyIdle();

            if (!owner_closing_.load(std::memory_order_acquire) &&
                queued_tasks_.load(std::memory_order_acquire) == 0 &&
                active_posts_.load(std::memory_order_acquire) == 0) {
                return;
            }

            bool expected = false;
            if (!draining_.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                   std::memory_order_acquire)) {
                return;
            }
            continue;
        }

        queued_tasks_.fetch_sub(1, std::memory_order_acq_rel);
        std::function<void()> task = std::move(node->task);
        delete node;
        task();
    }
}

ClientConnection::TaskNode* ClientConnection::PopAvailableTaskNode() {
    for (;;) {
        bool empty = false;
        TaskNode* node = static_cast<TaskNode*>(task_queue_.PopAndCheckEnd(&empty));
        if (node != nullptr) {
            return node;
        }
        if (empty) {
            return nullptr;
        }
        std::this_thread::yield();
    }
}

void ClientConnection::DrainTaskQueue() {
    for (;;) {
        TaskNode* node = PopAvailableTaskNode();
        if (node != nullptr) {
            delete node;
            queued_tasks_.fetch_sub(1, std::memory_order_acq_rel);
            continue;
        }
        if (active_posts_.load(std::memory_order_acquire) == 0 &&
            queued_tasks_.load(std::memory_order_acquire) == 0) {
            return;
        }
        std::this_thread::yield();
    }
}

void ClientConnection::NotifyIdle() {
    if (!owner_closing_.load(std::memory_order_acquire)) {
        return;
    }

    std::lock_guard<std::mutex> lock(idle_mtx_);
    if (active_posts_.load(std::memory_order_acquire) == 0) {
        idle_cv_.notify_all();
    }
}

void ClientConnection::ConnectResolved(const connx_resolved_address& addr) {
    std::string error;
    SocketHandle new_fd = InvalidSocketHandle();
    if (!PlatformOpenSocket(addr, &new_fd, &error)) {
        CONNX_LOG_WARN("connx client open socket failed error=%s", error.c_str());
        StartClose(CloseReason::kConnectFailed, error);
        return;
    }

    fd_ = new_fd;
    id_ = GlobalRuntime::Instance().connections().Register(shared_from_this());
    CONNX_LOG_DEBUG(
        "connx client connect start id=%" PRIu32 ":%" PRIu32 " fd=%" PRIuPTR " timeout_ms=%d",
        id_.slot, id_.generation, static_cast<uintptr_t>(fd_), opts_.tcp.connect_timeout_ms);

    if (opts_.tcp.connect_timeout_ms > 0) {
        connect_deadline_ms_ = GetCurrentMillisec() + opts_.tcp.connect_timeout_ms;
        GlobalRuntime::Instance().timers().Add(connect_deadline_ms_, id_,
                                               TimerKind::kConnectTimeout);
        GlobalRuntime::Instance().poller().Wake();
    }

    int rc = PlatformConnect(fd_, addr, &error);
    if (rc < 0) {
        StartClose(CloseReason::kConnectFailed, error);
        return;
    }
    if (rc == 0) {
        HandleConnected();
    }
}

void ClientConnection::HandlePollEvent(const PollEvent& ev) {
    if (ev.id != id_) {
        return;
    }

    if (ev.operation == kPollOpConnect) {
        HandleConnectCompletion(ev);
        return;
    }
    if (ev.operation == kPollOpRecv) {
        HandleRecvCompletion(ev);
        return;
    }
    if (ev.operation == kPollOpSend) {
        HandleSendCompletion(ev);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(state_mtx_);
        if (state_ == State::kIdle || state_ == State::kClosing) {
            return;
        }
    }

    if (ev.events & kPollEventError) {
        if (IsConnecting()) {
            std::string error;
            if (!PlatformFinishConnect(&error)) {
                if (error.empty()) {
                    error = FormatErrorMessage(ev.error_code);
                }
                StartClose(CloseReason::kConnectFailed, error);
                return;
            }
        }
        StartClose(IsConnected() ? CloseReason::kError : CloseReason::kConnectFailed,
                   FormatErrorMessage(ev.error_code));
        return;
    }

    if (IsConnecting() && (ev.events & (kPollEventReadable | kPollEventWritable))) {
        std::string error;
        if (!PlatformFinishConnect(&error)) {
            StartClose(CloseReason::kConnectFailed, error);
            return;
        }
        HandleConnected();
        if (!IsConnected()) {
            return;
        }
    }

    if (ev.events & kPollEventReadable) {
        HandleReadable();
        if (!IsConnected()) {
            return;
        }
    }
    if (ev.events & kPollEventWritable) {
        HandleWritable();
    }
}

void ClientConnection::HandleTimerEvent(const TimerEvent& ev) {
    if (ev.id != id_ || ev.kind != TimerKind::kConnectTimeout) {
        return;
    }
    bool expired = false;
    {
        std::lock_guard<std::mutex> lock(state_mtx_);
        expired = (state_ == State::kConnecting && connect_deadline_ms_ == ev.deadline_ms);
    }
    if (expired) {
        StartClose(CloseReason::kConnectTimeout, "connect timeout");
    }
}

void ClientConnection::HandleConnected() {
    {
        std::lock_guard<std::mutex> lock(state_mtx_);
        if (state_ != State::kConnecting) {
            return;
        }
        connect_deadline_ms_ = 0;
    }

    std::string error;
    if (!PlatformAfterConnected(&error)) {
        StartClose(CloseReason::kConnectFailed, error);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(state_mtx_);
        if (state_ != State::kConnecting) {
            return;
        }
        state_ = State::kConnected;
    }
    connected_.store(true, std::memory_order_release);
    CONNX_LOG_DEBUG("connx client connected id=%" PRIu32 ":%" PRIu32 " fd=%" PRIuPTR, id_.slot,
                    id_.generation, static_cast<uintptr_t>(fd_));
    if (handler_) {
        handler_->OnConnected();
    }
}

void ClientConnection::HandleReadable() { PlatformHandleReadable(); }

void ClientConnection::HandleWritable() { PlatformHandleWritable(); }

void ClientConnection::StartClose(CloseReason reason, const std::string& desc) {
    bool notify = false;
    ConnectionId close_id;
    {
        std::lock_guard<std::mutex> lock(state_mtx_);
        if (state_ == State::kIdle || state_ == State::kClosing) {
            return;
        }
        notify = (reason != CloseReason::kUser && reason != CloseReason::kRelease);
        state_ = State::kClosing;
        connected_.store(false, std::memory_order_release);
        connect_deadline_ms_ = 0;
        close_id = id_;
    }

    const char* reason_name = "unknown";
    switch (reason) {
    case CloseReason::kUser:
        reason_name = "user";
        break;
    case CloseReason::kConnectFailed:
        reason_name = "connect_failed";
        break;
    case CloseReason::kConnectTimeout:
        reason_name = "connect_timeout";
        break;
    case CloseReason::kRemoteClosed:
        reason_name = "remote_closed";
        break;
    case CloseReason::kError:
        reason_name = "error";
        break;
    case CloseReason::kRelease:
        reason_name = "release";
        break;
    }

    const char* close_desc = desc.empty() ? "-" : desc.c_str();
    if (reason == CloseReason::kError) {
        CONNX_LOG_ERROR(
            "connx client closing id=%" PRIu32 ":%" PRIu32 " reason=%s notify=%d desc=%s",
            close_id.slot, close_id.generation, reason_name, notify ? 1 : 0, close_desc);
    } else if (reason == CloseReason::kConnectFailed || reason == CloseReason::kConnectTimeout) {
        CONNX_LOG_WARN("connx client closing id=%" PRIu32 ":%" PRIu32
                       " reason=%s notify=%d desc=%s",
                       close_id.slot, close_id.generation, reason_name, notify ? 1 : 0, close_desc);
    } else {
        CONNX_LOG_DEBUG(
            "connx client closing id=%" PRIu32 ":%" PRIu32 " reason=%s notify=%d desc=%s",
            close_id.slot, close_id.generation, reason_name, notify ? 1 : 0, close_desc);
    }
    FinishClose(notify, reason, desc);
}

void ClientConnection::FinishClose(bool notify, CloseReason reason, const std::string& desc) {
    if (id_.valid()) {
        GlobalRuntime::Instance().poller().Remove(fd_);
        GlobalRuntime::Instance().connections().Unregister(id_);
        id_ = ConnectionId();
    }
    CloseFd();
    ResetAfterClose();

    {
        std::lock_guard<std::mutex> lock(state_mtx_);
        state_ = State::kIdle;
    }

    if (!notify || handler_ == nullptr) {
        return;
    }

    if (reason == CloseReason::kConnectFailed || reason == CloseReason::kConnectTimeout) {
        handler_->OnConnectFailed(desc.c_str());
    } else {
        handler_->OnClosed();
    }
}

void ClientConnection::ParseInput() {
    if (opts_.codec == nullptr || handler_ == nullptr) {
        return;
    }

    size_t total = recv_buffer_.GetBufferLength();
    if (total == 0) {
        return;
    }

    Slice merged = recv_buffer_.Merge();
    size_t offset = 0;
    while (offset < total) {
        size_t consumed = 0;
        DecodeResult result =
            opts_.codec->Decode(merged.data() + offset, total - offset, &consumed);
        if (result == DecodeResult::kNeedMoreData) {
            break;
        }
        if (result == DecodeResult::kError || consumed == 0 || consumed > total - offset) {
            StartClose(CloseReason::kError, "protocol decode error");
            return;
        }

        handler_->OnMessage(merged.data() + offset, consumed);
        if (!IsConnected()) {
            return;
        }
        offset += consumed;
    }

    if (offset > 0 && !recv_buffer_.MoveHeader(offset)) {
        StartClose(CloseReason::kError, "protocol buffer state error");
    }
}

void ClientConnection::UpdateWriteInterest() {
    if (!id_.valid() || !IsValidSocketHandle(fd_)) {
        return;
    }
    std::string error;
    if (!PlatformStartSend(&error)) {
        StartClose(CloseReason::kError, error);
    }
}

void ClientConnection::CloseFd() { PlatformCloseSocket(); }

void ClientConnection::ResetAfterClose() {
    poll_interest_ = 0;
    recv_buffer_.ClearBuffer();
    send_buffer_.ClearBuffer();
    pending_bytes_.store(0, std::memory_order_relaxed);
    PlatformResetIoState();
}

} // namespace connx
