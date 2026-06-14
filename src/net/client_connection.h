/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_NET_CLIENT_CONNECTION_H_
#define CONNX_SRC_NET_CLIENT_CONNECTION_H_

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "connx/client.h"
#include "src/net/connection_id.h"
#include "src/net/poller.h"
#include "src/net/resolve_address.h"
#include "src/net/timer_queue.h"
#include "src/utils/mpscq.h"
#include "src/utils/slice_buffer.h"

namespace connx {

class ClientConnection : public std::enable_shared_from_this<ClientConnection> {
public:
    ClientConnection(ClientHandler* handler, const ClientOptions& opts);
    ~ClientConnection();

    bool Connect(const char* hosts);
    void Disconnect();
    bool SendBuffer(const void* data, size_t len);
    bool IsConnected() const;
    bool IsConnecting() const;
    void GetMetrics(Metrics* out_metrics) const;
    void CloseFromOwner();

    void PostPollEvent(const PollEvent& ev);
    void PostTimerEvent(const TimerEvent& ev);

private:
    enum class State { kIdle, kConnecting, kConnected, kClosing };
    enum class CloseReason {
        kUser,
        kConnectFailed,
        kConnectTimeout,
        kRemoteClosed,
        kError,
        kRelease
    };
    enum class ParseStatus { kDone, kYielded, kClosed };

    struct TaskNode : public MultiProducerSingleConsumerQueue::Node {
        explicit TaskNode(const std::function<void()>& in_task)
            : task(in_task) {}
        std::function<void()> task;
    };

    void Post(const std::function<void()>& task);
    void ScheduleDrain(const std::shared_ptr<ClientConnection>& self);
    void RescheduleDrain(const std::shared_ptr<ClientConnection>& self);
    void Drain();
    TaskNode* PopAvailableTaskNode();
    void DrainTaskQueue();
    void NotifyIdle();

    void ConnectResolved(const connx_resolved_address& addr);
    void HandlePollEvent(const PollEvent& ev);
    void HandleTimerEvent(const TimerEvent& ev);
    void HandleConnected();
    void HandleReadable();
    void HandleWritable();
    void HandleConnectCompletion(const PollEvent& ev);
    void HandleRecvCompletion(const PollEvent& ev);
    void HandleSendCompletion(const PollEvent& ev);
    void StartClose(CloseReason reason, const std::string& desc);
    void FinishClose(bool notify, CloseReason reason, const std::string& desc);
    ParseStatus ParseInput();
    void ScheduleParseContinuation();
    void DeferCloseUntilInputDrained(CloseReason reason, const std::string& desc);
    void FinishDeferredClose();
    void UpdateWriteInterest();
    void CloseFd();
    void ResetAfterClose();

    bool PlatformOpenSocket(const connx_resolved_address& addr, SocketHandle* out_fd,
                            std::string* error);
    int PlatformConnect(SocketHandle fd, const connx_resolved_address& addr, std::string* error);
    bool PlatformFinishConnect(std::string* error);
    bool PlatformAfterConnected(std::string* error);
    void PlatformHandleReadable();
    void PlatformHandleWritable();
    void PlatformCloseSocket();
    bool PlatformStartRecv(std::string* error);
    bool PlatformStartSend(std::string* error);
    void PlatformResetIoState();

    ClientHandler* handler_;
    ClientOptions opts_;

    mutable std::mutex state_mtx_;
    State state_;
    ConnectionId id_;
    SocketHandle fd_;
    int poll_interest_;
    size_t worker_index_;
    int64_t connect_deadline_ms_;
    std::atomic<bool> connected_;
    std::atomic<uint64_t> bytes_sent_;
    std::atomic<uint64_t> bytes_received_;
    std::atomic<size_t> pending_bytes_;

    MultiProducerSingleConsumerQueue task_queue_;
    std::atomic<bool> accepting_tasks_;
    std::atomic<int> active_posts_;
    std::atomic<size_t> queued_tasks_;
    std::atomic<bool> owner_closing_;
    std::atomic<bool> draining_;
    bool drain_yield_requested_;
    bool parse_continuation_pending_;
    bool deferred_close_pending_;
    CloseReason deferred_close_reason_;
    std::string deferred_close_desc_;
    std::condition_variable idle_cv_;
    std::mutex idle_mtx_;

    SliceBuffer recv_buffer_;
    SliceBuffer send_buffer_;
    char recv_cache_[65536];

#ifdef _WIN32
    bool recv_pending_;
    bool send_pending_;
#endif
};

} // namespace connx

#endif // CONNX_SRC_NET_CLIENT_CONNECTION_H_
