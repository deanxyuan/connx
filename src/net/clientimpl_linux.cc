/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "src/net/clientimpl.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include "src/utils/string.h"
#include "src/net/socket_util.h"
#include "src/utils/status.h"
#include "src/utils/useful.h"
#include "src/utils/time.h"
#include "src/net/connect_timeout.h"

static connx::internal::LibraryInitializer g_lib_initializer;
static connx::ConnectTimeoutList g_connect_timeouts;

/* set a socket to non blocking mode */
connx_error connx_set_socket_nonblocking(int fd, int non_blocking) {
    int oldflags = fcntl(fd, F_GETFL, 0);
    if (oldflags < 0) {
        return CONNX_POSIX_ERROR(errno, "fcntl");
    }

    if (non_blocking) {
        oldflags |= O_NONBLOCK;
    } else {
        oldflags &= ~O_NONBLOCK;
    }

    if (fcntl(fd, F_SETFL, oldflags) != 0) {
        return CONNX_POSIX_ERROR(errno, "fcntl");
    }

    return CONNX_ERROR_NONE;
}
/* set a socket to close on exec */
connx_error connx_set_socket_cloexec(int fd, int close_on_exec) {
    int oldflags = fcntl(fd, F_GETFD, 0);
    if (oldflags < 0) {
        return CONNX_POSIX_ERROR(errno, "fcntl");
    }

    if (close_on_exec) {
        oldflags |= FD_CLOEXEC;
    } else {
        oldflags &= ~FD_CLOEXEC;
    }

    if (fcntl(fd, F_SETFD, oldflags) != 0) {
        return CONNX_POSIX_ERROR(errno, "fcntl");
    }

    return CONNX_ERROR_NONE;
}
/* disable nagle */
connx_error connx_set_socket_low_latency(int fd, int low_latency) {
    int val = (low_latency != 0);
    int newval;
    socklen_t intlen = sizeof(newval);
    if (0 != setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val))) {
        return CONNX_POSIX_ERROR(errno, "setsockopt(TCP_NODELAY)");
    }
    if (0 != getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &newval, &intlen)) {
        return CONNX_POSIX_ERROR(errno, "getsockopt(TCP_NODELAY)");
    }
    if ((newval != 0) != val) {
        return CONNX_ERROR_FROM_STATIC_STRING("Failed to set TCP_NODELAY");
    }
    return CONNX_ERROR_NONE;
}

connx_error connx_prepare_client_socket(int fd, const connx::TcpOptions& tcp) {
    connx_error ret = CONNX_ERROR_NONE;

    ret = connx_set_socket_nonblocking(fd, 1);
    if (ret != CONNX_ERROR_NONE) goto failure;

    ret = connx_set_socket_cloexec(fd, 1);
    if (ret != CONNX_ERROR_NONE) goto failure;

    if (tcp.tcp_nodelay) {
        ret = connx_set_socket_low_latency(fd, 1);
        if (ret != CONNX_ERROR_NONE) goto failure;
    }

#ifdef TCP_QUICKACK
    if (tcp.tcp_quickack) {
        int val = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &val, sizeof(val));
    }
#endif
    if (tcp.send_buffer_size > 0) {
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &tcp.send_buffer_size, sizeof(tcp.send_buffer_size));
    }
    if (tcp.recv_buffer_size > 0) {
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &tcp.recv_buffer_size, sizeof(tcp.recv_buffer_size));
    }
    if (tcp.linger_sec >= 0) {
        struct linger ling;
        ling.l_onoff = (tcp.linger_sec > 0) ? 1 : 0;
        ling.l_linger = tcp.linger_sec;
        setsockopt(fd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));
    }

    return ret;

failure:
    if (fd >= 0) {
        close(fd);
    }
    return ret;
}

static connx_error connx_create_client_socket(const connx_resolved_address* input, int* newfd,
                                              const connx::TcpOptions& tcp) {
    const connx_sockaddr* addr = reinterpret_cast<const connx_sockaddr*>(input->addr);

    *newfd = 0;

    int fd = socket(addr->sa_family, SOCK_STREAM, 0);
    if (fd < 0) {
        return CONNX_SYSTEM_ERROR(errno, "socket");
    }
    connx_error ret = connx_prepare_client_socket(fd, tcp);
    if (ret != CONNX_ERROR_NONE) return ret;

    *newfd = fd;
    return CONNX_ERROR_NONE;
}

#define MAX_EPOLL_EVENTS 256
static struct epoll_event g_epoll_events[MAX_EPOLL_EVENTS];
static int g_epoll_fd = -1;
static bool g_epoll_running = false;
static pthread_t g_epoll_thread = 0;
static connx_error EpollInit() {
    g_epoll_fd = epoll_create(MAX_EPOLL_EVENTS);
    if (g_epoll_fd < 0) {
        return CONNX_SYSTEM_ERROR(errno, "epoll_create");
    }
    if (fcntl(g_epoll_fd, F_SETFD, FD_CLOEXEC) != 0) {
        return CONNX_SYSTEM_ERROR(errno, "fcntl");
    }
    return CONNX_ERROR_NONE;
}

static int EpollAdd(int fd, void* data, uint32_t events) {
    epoll_event ev;
    ev.events = events;
    ev.data.ptr = data;
    return epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}

static int EpollModify(int fd, void* data, uint32_t events) {
    epoll_event ev;
    ev.events = events;
    ev.data.ptr = data;
    return epoll_ctl(g_epoll_fd, EPOLL_CTL_MOD, fd, &ev);
}

static int EpollDelete(int fd, uint32_t events) {
    epoll_event ev;
    ev.events = events;
    ev.data.ptr = 0;
    return epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, fd, &ev);
}

static int EpollWait(int timeout_ms) {
    return epoll_wait(g_epoll_fd, g_epoll_events, MAX_EPOLL_EVENTS, timeout_ms);
}

#define CLOSE_SOCKET(fd)                                                                           \
    if (fd != -1) {                                                                                \
        ::close(fd);                                                                               \
        fd = -1;                                                                                   \
    }

#define EPOLL_EVENT_ADDR(i) (&g_epoll_events[i])
namespace connx {

connx_error ClientImpl::Init() {
    connx_error r = EpollInit();
    if (r != CONNX_ERROR_NONE) {
        return r;
    }

    g_epoll_running = true;

    if (!g_epoll_thread) {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
        int result = pthread_create(&g_epoll_thread, &attr, ClientImpl::PollingThread, NULL);
        pthread_attr_destroy(&attr);
        if (result != 0) {
            return CONNX_SYSTEM_ERROR(errno, "pthread_create");
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
    g_epoll_running = false;

    CLOSE_SOCKET(g_epoll_fd);

    if (g_epoll_thread) {
        pthread_join(g_epoll_thread, NULL);
        g_epoll_thread = 0;
    }
}

void* ClientImpl::PollingThread(void*) {
    while (g_epoll_running) {

        int count = EpollWait(MIN_TIME_SLICE);

        for (int i = 0; i < count; i++) {
            struct epoll_event* ev = EPOLL_EVENT_ADDR(i);
            ClientImpl* connector = reinterpret_cast<ClientImpl*>(ev->data.ptr);

            if (!connector) continue;
            connector->Ref();

            if (connector->shutdown_) {
                connector->Unref();
                continue;
            }
            if (ev->events & EPOLLERR || ev->events & EPOLLHUP || ev->events & EPOLLRDHUP) {
                connector->OnErrorEvent(internal::kErrorEvent, errno);
                connector->Unref();
                continue;
            }

            if (!connector->is_connected_) {
                connector->last_recv_time_ = GetCurrentMillisec();
                connector->last_send_time_ = connector->last_recv_time_;
                connector->OnConnected();
            }

            if (ev->events & EPOLLIN) {
                connector->OnRecvEvent();
            }
            if (ev->events & EPOLLOUT) {
                connector->OnSendEvent();
            }
            connector->Unref();
        }
        g_connect_timeouts.CheckTimeouts();
    }
    return NULL;
}

ClientImpl::ClientImpl()
    : service_(nullptr)
    , shutdown_(true)
    , is_connected_(false)
    , poll_registered_(false)
    , fd_(-1)
    , next_package_size_(0)
    , last_send_time_(0)
    , last_recv_time_(0)
    , connect_deadline_(0)
    , number_of_bytes_sent_(0)
    , number_of_bytes_received_(0)
    , buffer_(nullptr)
    , msg_count_(0) {
    g_lib_initializer.summon();
}

ClientImpl::~ClientImpl() {
    if (buffer_) delete[] buffer_;
}

void ClientImpl::Start(ClientHandler* s) {
    if (shutdown_) {
        if (buffer_ == nullptr) {
            buffer_ = new char[RECV_CACHE_SIZE];
        }
        service_ = s;
        BeforeStartWorkThread();
    }
}

void ClientImpl::Stop() {
    if (!shutdown_) {
        CONNX_LOG_DEBUG("client shutdown...");
        shutdown_ = true;
        ClearConnectDeadline(this);
        EpollDelete(fd_, EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP);
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
connx_error ClientImpl::ConnectImpl(const connx_resolved_address* addr) {
    if (fd_ != -1) {
        return CONNX_ERROR_FROM_STATIC_STRING("the socket has not been closed");
    }

    int sock_fd = -1;
    connx_error ret = connx_create_client_socket(addr, &sock_fd, opt_.tcp);
    if (ret != CONNX_ERROR_NONE) {
        return ret;
    }

    if (opt_.local_address != nullptr) {
        connx_resolved_address local_addr;
        if (!connx_string_to_sockaddr(&local_addr, opt_.local_address, 0)) {
            CLOSE_SOCKET(sock_fd);
            return CONNX_ERROR_FROM_STATIC_STRING("invalid local address");
        }
        if (bind(sock_fd, (const connx_sockaddr*)local_addr.addr, local_addr.len) != 0) {
            ret = CONNX_SYSTEM_ERROR(errno, "bind");
            CLOSE_SOCKET(sock_fd);
            return ret;
        }
    }

    if (opt_.tcp.connect_timeout_ms > 0) {
        connect_deadline_ = GetCurrentMillisec() + opt_.tcp.connect_timeout_ms;
        g_connect_timeouts.Register(this); // beforce connect
    }

    int err = 0;
    do {
        err = connect(sock_fd, (const connx_sockaddr*)addr->addr, addr->len);
    } while (err < 0 && errno == EINTR);

    if (err < 0 && errno != EWOULDBLOCK && errno != EINPROGRESS) {
        if (opt_.tcp.connect_timeout_ms > 0) {
            connect_deadline_ = 0;
            g_connect_timeouts.Unregister(this);
        }
        ret = CONNX_SYSTEM_ERROR(errno, "connect");
        CLOSE_SOCKET(sock_fd);
        return ret;
    }
    Ref();
    fd_ = sock_fd; // beforce poll_registered_
    poll_registered_.store(true, std::memory_order_release);
    if (EpollAdd(sock_fd, this, EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP) != 0) {
        ret = CONNX_SYSTEM_ERROR(errno, "epoll_ctl");
        fd_ = -1;
        poll_registered_.store(false, std::memory_order_release);
        Unref();
        ::close(sock_fd);
        return ret;
    }

    return CONNX_ERROR_NONE;
}

bool ClientImpl::SendMsg(const Slice& msg) {
    if (shutdown_ || msg.empty()) {
        return false;
    }
    std::unique_lock<std::mutex> g(smtx_);
    send_buffer_.AddSlice(msg);
    EpollModify(fd_, this, EPOLLIN | EPOLLOUT | EPOLLET);
    return true;
}
bool ClientImpl::SendMsg(Slice&& msg) {
    if (shutdown_ || msg.empty()) {
        return false;
    }
    std::unique_lock<std::mutex> g(smtx_);
    send_buffer_.AddSlice(std::move(msg));
    EpollModify(fd_, this, EPOLLIN | EPOLLOUT | EPOLLET);
    return true;
}

void ClientImpl::Disconnect() {
    if (fd_ == -1 || !is_connected_) return;
    EpollDelete(fd_, EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP);
    // is_connected_ = false; // managed by OnErrorEvent
    ReleasePollRef();
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

void ClientImpl::OnSendEvent() {
    std::unique_lock<std::mutex> g(smtx_);
    if (!send_buffer_.Empty() && SendImpl() != 0) {
        OnErrorEvent(internal::kSendEvent, errno);
        return;
    }
    if (send_buffer_.Empty()) {
        EpollModify(fd_, this, EPOLLIN | EPOLLET);
    } else {
        EpollModify(fd_, this, EPOLLIN | EPOLLOUT | EPOLLET);
    }
}
void ClientImpl::OnRecvEvent() {
    if (RecvImpl() != 0) {
        OnErrorEvent(internal::kRecvEvent, errno);
        return;
    }
    if (send_buffer_.Empty()) {
        EpollModify(fd_, this, EPOLLIN | EPOLLET);
    } else {
        EpollModify(fd_, this, EPOLLIN | EPOLLOUT | EPOLLET);
    }
}

int ClientImpl::RecvImpl() {

    int unused_space = RECV_CACHE_SIZE;
    int recv_bytes = 0;

    do {
        recv_bytes = ::recv(fd_, buffer_, unused_space, 0);
        if (recv_bytes == 0) return -1;
        if (recv_bytes < 0) {
            if (errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN) {
                break;
            }
            return -1;
        }
        last_recv_time_ = GetCurrentMillisec();
        number_of_bytes_received_ += recv_bytes;

        TransferData(Slice(buffer_, recv_bytes));

    } while (recv_bytes == unused_space);

    return 0;
}

int ClientImpl::SendImpl() {
    size_t count = 0;
    do {
        Slice slice = send_buffer_.Front();
        int slen = ::send(fd_, slice.begin(), slice.size(), 0);
        if (slen == 0) {
            break;
        }

        if (slen < 0) {
            if (errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN) {
                break;
            }
            return -1;
        }
        last_send_time_ = GetCurrentMillisec();
        number_of_bytes_sent_ += slen;

        send_buffer_.MoveHeader((size_t)slen);
        count = send_buffer_.SliceCount();

    } while (count > 0);
    return 0;
}
bool ClientImpl::ReleasePollRef() {
    if (poll_registered_.exchange(false, std::memory_order_acq_rel)) {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        Unref();
        return true;
    }
    return false;
}
} // namespace connx
