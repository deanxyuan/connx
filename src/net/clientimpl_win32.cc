/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "src/net/clientimpl.h"
#include "src/utils/status.h"
#include "src/utils/useful.h"
#include "src/utils/time.h"
#include "src/utils/string.h"
#include "src/net/socket_util.h"
#include "src/net/connect_timeout.h"

static connx::internal::LibraryInitializer g_lib_initializer;
static connx::ConnectTimeoutList g_connect_timeouts;
static LPFN_CONNECTEX g_connectex = nullptr;
static connx::AtomicBool g_connectex_lock{false};

static connx_error connx_get_connectex(SOCKET s) {
    // Fast path: already cached
    if (g_connectex) {
        return CONNX_ERROR_NONE;
    }

    // Spin acquire. While waiting, re-check g_connectex in case the
    // lock holder has already finished initializing.
    while (g_connectex_lock.Exchange(true, connx::MemoryOrder::ACQUIRE)) {
        if (g_connectex) {
            return CONNX_ERROR_NONE;
        }
    }

    connx_error err = CONNX_ERROR_NONE;
    if (!g_connectex) {
        GUID guid = WSAID_CONNECTEX;
        DWORD bytes;
        LPFN_CONNECTEX fn = nullptr;
        int status = WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &fn,
                              sizeof(fn), &bytes, NULL, NULL);
        if (status != 0) {
            err = CONNX_SYSTEM_ERROR(WSAGetLastError(),
                                     "WSAIoctl(SIO_GET_EXTENSION_FUNCTION_POINTER)");
        } else {
            g_connectex = fn;
        }
    }

    g_connectex_lock.Store(false, connx::MemoryOrder::RELEASE);
    return err;
}

/* set a socket to non blocking mode */
connx_error connx_set_socket_nonblocking(SOCKET fd, int non_blocking) {
    uint32_t param = non_blocking ? 1 : 0;
    DWORD BytesReturned;
    int status = WSAIoctl(fd, FIONBIO, &param, sizeof(param), NULL, 0, &BytesReturned, NULL, NULL);
    return status == 0 ? CONNX_ERROR_NONE
                       : CONNX_SYSTEM_ERROR(WSAGetLastError(), "WSAIoctl(FIONBIO)");
}

/* disable nagle */
connx_error connx_set_socket_low_latency(SOCKET fd, int low_latency) {
    int val = low_latency ? 1 : 0;
    int status = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&val, sizeof(val));

    return status == 0 ? CONNX_ERROR_NONE
                       : CONNX_SYSTEM_ERROR(WSAGetLastError(), "setsockopt(TCP_NODELAY)");
}

static inline SOCKET ws2_socket(int family, int type, int protocol) {
    return WSASocket(family, type, protocol, NULL, 0,
                     WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
}

static connx_error connx_prepare_client_socket(SOCKET fd, const connx::TcpOptions& tcp) {
    connx_error ret = CONNX_ERROR_NONE;

    ret = connx_set_socket_nonblocking(fd, 1);
    if (ret != CONNX_ERROR_NONE) return ret;

    if (tcp.tcp_nodelay) {
        ret = connx_set_socket_low_latency(fd, 1);
        if (ret != CONNX_ERROR_NONE) return ret;
    }

    if (tcp.send_buffer_size > 0) {
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (const char*)&tcp.send_buffer_size,
                   sizeof(tcp.send_buffer_size));
    }
    if (tcp.recv_buffer_size > 0) {
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (const char*)&tcp.recv_buffer_size,
                   sizeof(tcp.recv_buffer_size));
    }

    if (tcp.linger_sec >= 0) {
        struct linger ling;
        ling.l_onoff = (tcp.linger_sec > 0) ? 1 : 0;
        ling.l_linger = (u_short)tcp.linger_sec;
        setsockopt(fd, SOL_SOCKET, SO_LINGER, (const char*)&ling, sizeof(ling));
    }

    return CONNX_ERROR_NONE;
}

struct OverlappedEx {
    OVERLAPPED overlapped;
    int event_type;
    SOCKET fd;
};

static bool g_iocp_running = false;
static HANDLE g_iocp_handle = NULL;
static HANDLE g_iocp_thread = NULL;

static inline connx_error IocpCreate() {
    if (!g_iocp_handle) {
        g_iocp_handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
        if (g_iocp_handle == NULL) {
            return CONNX_SYSTEM_ERROR(GetLastError(), "CreateIoCompletionPort");
        }
    }
    return CONNX_ERROR_NONE;
}
static inline bool IocpAdd(SOCKET sock, void* CompletionKey) {
    if (g_iocp_handle == NULL) return false;

    HANDLE h = CreateIoCompletionPort((HANDLE)sock, g_iocp_handle, (ULONG_PTR)CompletionKey, 0);
    return h != NULL;
}

static inline bool IocpPolling(DWORD* NumberOfBytesTransferred, PULONG_PTR CompletionKey,
                               LPOVERLAPPED* lpOverlapped, DWORD timeout_ms) {
    BOOL r = GetQueuedCompletionStatus(g_iocp_handle, NumberOfBytesTransferred, CompletionKey,
                                       lpOverlapped, timeout_ms);
    return (r != FALSE);
}

#define CLOSE_HANDLE(h)                                                                            \
    if (h != NULL) {                                                                               \
        CloseHandle(h);                                                                            \
        h = NULL;                                                                                  \
    }

#define CLOSE_SOCKET(s)                                                                            \
    if (s != INVALID_SOCKET) {                                                                     \
        closesocket(s);                                                                            \
        s = INVALID_SOCKET;                                                                        \
    }

namespace connx {

connx_error ClientImpl::Init() {
    connx_error r = IocpCreate();
    if (r != CONNX_ERROR_NONE) {
        return r;
    }

    g_iocp_running = true;

    if (!g_iocp_thread) {
        g_iocp_thread = CreateThread(NULL, 0, ClientImpl::PollingThread, NULL, 0, NULL);
        if (g_iocp_thread == NULL) {
            return CONNX_SYSTEM_ERROR(GetLastError(), "CreateThread");
        }
    }
    return CONNX_ERROR_NONE;
}
void ClientImpl::ClearConnectDeadline(ClientImpl* impl) {
    if (impl->connect_deadline_ != 0) {
        impl->connect_deadline_ = 0;
        g_connect_timeouts.Unregister(impl);
    }
}

void ClientImpl::Shutdown() {
    g_iocp_running = false;
    CLOSE_HANDLE(g_iocp_handle);
    CLOSE_HANDLE(g_iocp_thread);
}

DWORD ClientImpl::PollingThread(void*) {
    while (g_iocp_running) {

        DWORD NumberOfBytesTransferred = 0;
        void* CompletionKey = NULL;
        LPOVERLAPPED lpOverlapped = NULL;

        // https://docs.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getqueuedcompletionstatus
        bool ret = IocpPolling(&NumberOfBytesTransferred, (PULONG_PTR)&CompletionKey, &lpOverlapped,
                               MIN_TIME_SLICE);

        if (!ret) {
            /*
                If theGetQueuedCompletionStatus function succeeds, it dequeued a completion
                packet for a successful I/O operation from the completion port and has
                stored information in the variables pointed to by the following parameters:
                lpNumberOfBytes, lpCompletionKey, and lpOverlapped. Upon failure (the return
                value is FALSE), those same parameters can contain particular value
                combinations as follows:

                    (1) If *lpOverlapped is NULL, the function did not dequeue a completion
                        packet from the completion port. In this case, the function does not
                        store information in the variables pointed to by the lpNumberOfBytes
                        and lpCompletionKey parameters, and their values are indeterminate.

                    (2) If *lpOverlapped is not NULL and the function dequeues a completion
                        packet for a failed I/O operation from the completion port, the
                        function stores information about the failed operation in the
                        variables pointed to by lpNumberOfBytes, lpCompletionKey, and
                        lpOverlapped. To get extended error information, call GetLastError.
            */

            if (lpOverlapped != NULL && CompletionKey != NULL) {
                // Maybe an error occurred or the connection was closed

                OverlappedEx* olex = (OverlappedEx*)lpOverlapped;

                int err = GetLastError();
                ClientImpl* connector = (ClientImpl*)CompletionKey;
                connector->Ref();
                connector->DecIoPendingCounts();
                if (err == ERROR_OPERATION_ABORTED) {
                    connector->cve_.notify_one();
                } else {
                    connector->OnErrorEvent(olex->event_type, err);
                }
                connector->Unref();
            } else {
                // IOCP TIMEOUT - still need to check connect deadlines.
            }
            g_connect_timeouts.CheckTimeouts();
            continue;
        }
        OverlappedEx* olex = (OverlappedEx*)lpOverlapped;
        ClientImpl* connector = (ClientImpl*)CompletionKey;
        connector->Ref();
        connector->DecIoPendingCounts();

        if (!connector->is_connected_ && olex->event_type & internal::kConnectEvent) {
            if (!connector->AsyncRecv()) {
                connector->OnErrorEvent(internal::kRecvEvent, WSAGetLastError());
            } else {
                connector->last_send_time_ = GetCurrentMillisec();
                connector->last_recv_time_ = connector->last_send_time_;
                connector->OnConnected();
            }
            connector->Unref();
            continue;
        }
        if (olex->event_type & internal::kSendEvent) {
            connector->OnSendEvent(NumberOfBytesTransferred);
        } else if (olex->event_type & internal::kRecvEvent) {
            connector->OnRecvEvent(NumberOfBytesTransferred);
        }
        connector->Unref();
        g_connect_timeouts.CheckTimeouts();
    }
    return 0;
}

ClientImpl::ClientImpl()
    : service_(nullptr)
    , shutdown_(true)
    , is_connected_(false)
    , poll_registered_(false)
    , fd_(INVALID_SOCKET)
    , next_package_size_(0)
    , last_send_time_(0)
    , last_recv_time_(0)
    , connect_deadline_(0)
    , number_of_bytes_sent_(0)
    , number_of_bytes_received_(0)
    , pending_io_(0)
    , send_pending_(false)
    , conn_ole_(nullptr)
    , send_ole_(nullptr)
    , recv_ole_(nullptr)
    , buffer_(nullptr)
    , msg_count_(0) {
    g_lib_initializer.summon();
}

ClientImpl::~ClientImpl() {
    if (buffer_) delete[] buffer_;
    if (conn_ole_) delete conn_ole_;
    if (send_ole_) delete send_ole_;
    if (recv_ole_) delete recv_ole_;
}
void ClientImpl::Start(ClientHandler* s) {
    if (shutdown_) {
        if (buffer_ == nullptr) {
            buffer_ = new char[RECV_CACHE_SIZE];
        }
        service_ = s;
        if (conn_ole_ == nullptr) {
            conn_ole_ = new OverlappedEx;
        }
        if (send_ole_ == nullptr) {
            send_ole_ = new OverlappedEx;
        }
        if (recv_ole_ == nullptr) {
            recv_ole_ = new OverlappedEx;
        }
        memset(conn_ole_, 0, sizeof(*conn_ole_));
        memset(send_ole_, 0, sizeof(*send_ole_));
        memset(recv_ole_, 0, sizeof(*recv_ole_));
        conn_ole_->event_type = internal::kConnectEvent;
        send_ole_->event_type = internal::kSendEvent;
        recv_ole_->event_type = internal::kRecvEvent;
        pending_io_.Store(0);
        send_pending_ = false;
        BeforeStartWorkThread();
    }
}

void ClientImpl::Stop() {
    if (!shutdown_) {
        CONNX_LOG_DEBUG("client shutdown...");
        shutdown_ = true;
        ClearConnectDeadline(this);
        CancelIoEx((HANDLE)fd_, NULL);
        {
            std::unique_lock<std::mutex> lck(emtx_);
            cve_.wait_for(lck, std::chrono::milliseconds(MIN_TIME_SLICE * 2),
                          [this]() -> bool { return pending_io_.Load() == 0; });
        }
        is_connected_ = false;
        cv_.notify_one();

        if (thd_.joinable()) {
            thd_.join();
        }

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
        ReleasePollRef();
    }
}
void ClientImpl::Disconnect() {
    if (fd_ == INVALID_SOCKET || !is_connected_) return;
    CancelIoEx((HANDLE)fd_, NULL);
    {
        std::unique_lock<std::mutex> lck(emtx_);
        cve_.wait_for(lck, std::chrono::milliseconds(MIN_TIME_SLICE * 2),
                      [this]() -> bool { return pending_io_.Load() == 0; });
    }
    // is_connected_ = false; // managed by OnErrorEvent
    ReleasePollRef();
}

connx_error ClientImpl::ConnectImpl(const connx_resolved_address* addr) {
    if (shutdown_) return CONNX_ERROR_FROM_STATIC_STRING("client has been shutdown");

    connx_resolved_address local;
    SOCKET fd;
    int status;
    BOOL success;
    connx_error ret;

    const connx_sockaddr* tmp = reinterpret_cast<const connx_sockaddr*>(addr->addr);

    fd = ws2_socket(tmp->sa_family, SOCK_STREAM, static_cast<int>(IPPROTO_TCP));
    if (fd == INVALID_SOCKET) {
        ret = CONNX_SYSTEM_ERROR(WSAGetLastError(), "WSASocket");
        goto failure;
    }

    connx_prepare_client_socket(fd, opt_.tcp);

    ret = connx_get_connectex(fd);
    if (ret != CONNX_ERROR_NONE) {
        goto failure;
    }

    if (opt_.local_address != nullptr) {
        if (!connx_string_to_sockaddr(&local, opt_.local_address, 0)) {
            ret = CONNX_ERROR_FROM_STATIC_STRING("invalid local_address");
            goto failure;
        }
    } else {
        connx_sockaddr_make_wildcard4(0, &local);
    }

    status = bind(fd, (connx_sockaddr*)&local.addr, (int)local.len);
    if (status != 0) {
        ret = CONNX_SYSTEM_ERROR(WSAGetLastError(), "bind");
        goto failure;
    }

    Ref();
    fd_ = fd; // beforce poll_registered_
    poll_registered_.store(true, std::memory_order_release);
    if (!IocpAdd(fd, this)) {
        ret = CONNX_SYSTEM_ERROR(WSAGetLastError(), "CreateIoCompletionPort");
        fd_ = INVALID_SOCKET;
        poll_registered_.store(false, std::memory_order_release);
        Unref();
        closesocket(fd);
        return ret;
    }
    if (opt_.tcp.connect_timeout_ms > 0) {
        connect_deadline_ = GetCurrentMillisec() + opt_.tcp.connect_timeout_ms;
        g_connect_timeouts.Register(this); // beforce connect
    }

    success = g_connectex(fd, (connx_sockaddr*)addr->addr, (int)addr->len, NULL, 0, NULL,
                          &conn_ole_->overlapped);
    if (!success) {
        int err = WSAGetLastError();
        if (err != ERROR_IO_PENDING) {
            if (opt_.tcp.connect_timeout_ms > 0) {
                connect_deadline_ = 0;
                g_connect_timeouts.Unregister(this);
            }
            ret = CONNX_SYSTEM_ERROR(err, "ConnectEx");
            goto failure;
        }
    }
    IncIoPendingCounts();

    return CONNX_ERROR_NONE;

failure:
    if (!ReleasePollRef() && fd != INVALID_SOCKET) {
        closesocket(fd);
    }
    fd_ = INVALID_SOCKET;
    return ret;
}
bool ClientImpl::SendMsg(const Slice& msg) {
    if (shutdown_ || msg.empty()) {
        return false;
    }
    std::unique_lock<std::mutex> g(smtx_);
    send_buffer_.AddSlice(msg);
    return AsyncSend();
}
bool ClientImpl::SendMsg(Slice&& msg) {
    if (shutdown_ || msg.empty()) {
        return false;
    }
    std::unique_lock<std::mutex> g(smtx_);
    send_buffer_.AddSlice(std::move(msg));
    return AsyncSend();
}

void ClientImpl::OnErrorEvent(int event_type, int err) {
    // clean up connection resources
    ClearConnectDeadline(this);

    if (!ReleasePollRef()) {
        // fd has already been released, event has already been sent
        return;
    }

    // CAS: only the first caller can obtain the old connection state
    bool was_connected = is_connected_.exchange(false);
    auto obj = new ClientImpl::EventNode;
    if (err != 0) {
        CONNX_LOG_DEBUG("client error event.type:%d,err:%d", event_type, err);
        obj->desc = FormatErrorMessage(err);
    }

    if (was_connected) {
        obj->ev = NetEvent::kClosed;
    } else {
        obj->ev = NetEvent::kConnectFailed;
    }
    PostEventNode(obj);
}

void ClientImpl::OnSendEvent(DWORD send_bytes) {
    if (send_bytes == 0) return;
    if (!SendImpl(send_bytes)) {
        int err = WSAGetLastError();
        OnErrorEvent(internal::kSendEvent, err);
        return;
    }
}
void ClientImpl::OnRecvEvent(DWORD recv_bytes) {
    if (!RecvImpl(recv_bytes) || !AsyncRecv()) {
        int err = WSAGetLastError();
        OnErrorEvent(internal::kRecvEvent, err);
        return;
    }
}
bool ClientImpl::SendImpl(DWORD size) {
    std::unique_lock<std::mutex> g(smtx_);
    send_pending_ = false;
    send_buffer_.MoveHeader(size);
    number_of_bytes_sent_ += size;
    last_send_time_ = GetCurrentMillisec();
    return AsyncSend();
}

constexpr size_t MAX_PACKAGE_SIZE = 1024 * 1024;
constexpr size_t MAX_WSABUF_COUNT = 16;

bool ClientImpl::AsyncSend() {
    if (send_buffer_.Empty() || send_pending_ || shutdown_) {
        return true;
    }

    WSABUF buffers[MAX_WSABUF_COUNT];
    size_t prepare_send_size = 0;

    send_pending_ = true;

    size_t i, count = send_buffer_.SliceCount();
    for (i = 0; i < count && i < MAX_WSABUF_COUNT; i++) {
        const Slice& s = send_buffer_[i];

        if (i != 0 && prepare_send_size + s.size() > MAX_PACKAGE_SIZE) {
            break;
        }

        buffers[i].buf = s.buffer();
        buffers[i].len = static_cast<ULONG>(s.size());
        prepare_send_size += s.size();
    }

    // https://docs.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsasend
    int ret = WSASend(fd_, buffers, (DWORD)i, NULL, 0, &send_ole_->overlapped, NULL);

    if (ret == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            send_pending_ = false;
            return false;
        }
    }
    IncIoPendingCounts();
    return true;
}

bool ClientImpl::AsyncRecv() {
    if (shutdown_) return true;

    DWORD dwFlags = 0;
    WSABUF buffer;

    buffer.buf = NULL;
    buffer.len = 0;

    // https://docs.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsarecv
    int ret = WSARecv(fd_, &buffer, 1, NULL, &dwFlags, &recv_ole_->overlapped, NULL);

    if (ret == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            return false;
        }
    }
    IncIoPendingCounts();
    return true;
}

bool ClientImpl::RecvImpl(DWORD size) {
    (size);

    int unused_space = RECV_CACHE_SIZE;
    int recv_bytes = 0;

    do {
        recv_bytes = ::recv(fd_, buffer_, unused_space, 0);
        if (recv_bytes == 0) return false;
        if (recv_bytes < 0) {
            int err = WSAGetLastError();
            if (WSAEWOULDBLOCK == err) {
                break;
            }
            return false;
        }
        last_recv_time_ = GetCurrentMillisec();
        number_of_bytes_received_ += recv_bytes;

        TransferData(Slice(buffer_, recv_bytes));

    } while (recv_bytes == unused_space);

    return true;
}
void ClientImpl::IncIoPendingCounts() { pending_io_.FetchAdd(1, MemoryOrder::RELAXED); }
void ClientImpl::DecIoPendingCounts() { pending_io_.FetchSub(1, MemoryOrder::RELAXED); }
bool ClientImpl::ReleasePollRef() {
    if (poll_registered_.exchange(false, std::memory_order_acq_rel)) {
        if (fd_ != INVALID_SOCKET) {
            closesocket(fd_);
            fd_ = INVALID_SOCKET;
        }
        Unref();
        return true;
    }
    return false;
}
} // namespace connx
