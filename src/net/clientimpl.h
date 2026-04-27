/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_NET_CONNECTIMPL_H_
#define CONNX_SRC_NET_CONNECTIMPL_H_

#include <mutex>
#include <condition_variable>
#include <functional>
#include <string>
#include <thread>

#include "connx/client.h"
#include "src/core/library.h"
#include "src/utils/mpscq.h"
#include "src/utils/slice.h"
#include "src/net/sockaddr.h"
#include "src/net/resolve_address.h"
#include "src/utils/slice_buffer.h"
#include "src/utils/refcounted.h"

#ifdef _WIN32
struct OverlappedEx;
#endif

#define RECV_CACHE_SIZE 65536
#define MIN_TIME_SLICE  300
namespace connx {
enum class NetEvent : int { kNull = 0, kConnectFailed, kConnectSuccess, kClosed };

namespace internal {
constexpr int kErrorEvent = 1 << 0;
constexpr int kSendEvent = 1 << 1;
constexpr int kRecvEvent = 1 << 2;
constexpr int kConnectEvent = 1 << 3;
} // namespace internal

class ClientImpl : public LibraryInitService, public RefCounted<ClientImpl> {

    friend class ConnectTimeoutList;

    struct EventNode {
        MultiProducerSingleConsumerQueue::Node _;
        NetEvent ev;
        Slice data;
        std::string desc;
    };

public:
    static connx_error Init();
    static void Shutdown();
    static void ClearConnectDeadline(ClientImpl* impl);
    ClientImpl(/* args */);
    ~ClientImpl();

    void SetOptions(const ClientOptions& opts);
    void Start(ClientHandler* s);
    void Stop();

    bool Connect(const char* hosts);
    void Disconnect();

    bool SendMsg(const Slice& msg);
    bool SendMsg(Slice&& msg);
    bool IsConnected() const;
    bool IsConnecting() const;
    void GetMetrics(Metrics* out_metrics) const;

private:
    void BeforeStartWorkThread();
    connx_error ConnectImpl(const connx_resolved_address* addr);
    void OnErrorEvent(int event_type, int err);
#ifdef _WIN32
#    ifdef WIN64
    static DWORD PollingThread(void*);
#    else
    static DWORD __stdcall PollingThread(void*);
#    endif

    void OnSendEvent(DWORD bytes);
    void OnRecvEvent(DWORD bytes);
    bool AsyncSend();
    bool AsyncRecv();
    bool RecvImpl(DWORD size);
    bool SendImpl(DWORD size);
    void IncIoPendingCounts();
    void DecIoPendingCounts();
#else
    static void* PollingThread(void*);
    void OnSendEvent();
    void OnRecvEvent();
    int RecvImpl();
    int SendImpl();
#endif
    void OnConnected();
    void TransferData(const Slice& s);
    void ParsingProtocol();
    void WorkThread();
    inline void PostEventNode(ClientImpl::EventNode* node) {
        mpscq_.push(&node->_);
        msg_count_.FetchAdd(1, MemoryOrder::RELAXED);
        cv_.notify_one();
    }
    void OnDispatch(ClientImpl::EventNode* node);
    bool ReleasePollRef();

private:
    ClientHandler* service_;
    std::atomic<bool> shutdown_;
    std::atomic<bool> is_connected_;
    std::atomic<bool> poll_registered_;
#ifdef _WIN32
    SOCKET fd_;
#else
    int fd_;
#endif
    size_t next_package_size_;
    int64_t last_send_time_;
    int64_t last_recv_time_;
    int64_t connect_deadline_; // 0 = no timeout or not connecting
    std::atomic<uint64_t> number_of_bytes_sent_;
    std::atomic<uint64_t> number_of_bytes_received_;
#ifdef _WIN32
    AtomicInt32 pending_io_;
    bool send_pending_;
    struct OverlappedEx* conn_ole_;
    struct OverlappedEx* send_ole_;
    struct OverlappedEx* recv_ole_;
#endif

    char* buffer_;
    AtomicInt64 msg_count_; // num of msg that pending in mpscq_

    SliceBuffer recv_buffer_;
    SliceBuffer send_buffer_;
    std::mutex smtx_; // for send_buffer
    std::mutex cmtx_; // for cv_;
    std::thread thd_;
    std::condition_variable cv_;
#ifdef _WIN32
    std::mutex emtx_;
    std::condition_variable cve_;
#endif
    MultiProducerSingleConsumerQueue mpscq_;
    ClientOptions opt_;
};

} // namespace connx
#endif // CONNX_SRC_NET_CONNECTIMPL_H_
