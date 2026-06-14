/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef _WIN32

#include "src/net/client_connection.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "src/net/runtime.h"
#include "src/net/sockaddr.h"
#include "src/net/socket_util.h"
#include "src/utils/string.h"

namespace connx {
namespace {

constexpr size_t kMaxReadBytesPerEvent = 1024 * 1024;
constexpr size_t kMaxWriteBytesPerEvent = 1024 * 1024;

void CloseSocket(SocketHandle* fd) {
    if (IsValidSocketHandle(*fd)) {
        close(*fd);
        *fd = InvalidSocketHandle();
    }
}

bool SetNonBlocking(SocketHandle fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool SetCloseOnExec(SocketHandle fd) {
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

bool ApplyTcpOptions(SocketHandle fd, const TcpOptions& tcp, std::string* error) {
    if (!SetNonBlocking(fd)) {
        if (error) *error = FormatErrorMessage(errno);
        return false;
    }
    if (!SetCloseOnExec(fd)) {
        if (error) *error = FormatErrorMessage(errno);
        return false;
    }
#ifdef SO_NOSIGPIPE
    {
        int val = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val)) != 0) {
            if (error) *error = FormatErrorMessage(errno);
            return false;
        }
    }
#endif
    if (tcp.tcp_nodelay) {
        int val = 1;
        if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) != 0) {
            if (error) *error = FormatErrorMessage(errno);
            return false;
        }
    }
#ifdef TCP_QUICKACK
    if (tcp.tcp_quickack) {
        int val = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &val, sizeof(val));
    }
#endif
    if (tcp.send_buffer_size > 0) {
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &tcp.send_buffer_size,
                   sizeof(tcp.send_buffer_size));
    }
    if (tcp.recv_buffer_size > 0) {
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &tcp.recv_buffer_size,
                   sizeof(tcp.recv_buffer_size));
    }
    if (tcp.linger_sec >= 0) {
        struct linger ling;
        ling.l_onoff = (tcp.linger_sec > 0) ? 1 : 0;
        ling.l_linger = tcp.linger_sec;
        setsockopt(fd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));
    }
    return true;
}

ssize_t SendNoSignal(SocketHandle fd, const char* data, size_t len) {
#ifdef MSG_NOSIGNAL
    return send(fd, data, len, MSG_NOSIGNAL);
#else
    return send(fd, data, len, 0);
#endif
}

} // namespace

bool ClientConnection::PlatformOpenSocket(const connx_resolved_address& addr,
                                          SocketHandle* out_fd, std::string* error) {
    const connx_sockaddr* sa = reinterpret_cast<const connx_sockaddr*>(addr.addr);
    SocketHandle fd = socket(sa->sa_family, SOCK_STREAM, 0);
    if (!IsValidSocketHandle(fd)) {
        if (error) *error = FormatErrorMessage(errno);
        return false;
    }

    if (!ApplyTcpOptions(fd, opts_.tcp, error)) {
        CloseSocket(&fd);
        return false;
    }

    if (opts_.local_address != nullptr) {
        connx_resolved_address local_addr;
        if (!connx_string_to_sockaddr(&local_addr, opts_.local_address, 0)) {
            if (error) *error = "invalid local address";
            CloseSocket(&fd);
            return false;
        }
        if (bind(fd, reinterpret_cast<const connx_sockaddr*>(local_addr.addr),
                 static_cast<socklen_t>(local_addr.len)) != 0) {
            if (error) *error = FormatErrorMessage(errno);
            CloseSocket(&fd);
            return false;
        }
    }

    *out_fd = fd;
    return true;
}

int ClientConnection::PlatformConnect(SocketHandle fd, const connx_resolved_address& addr,
                                      std::string* error) {
    int rc = 0;
    do {
        rc = connect(fd, reinterpret_cast<const connx_sockaddr*>(addr.addr),
                     static_cast<socklen_t>(addr.len));
    } while (rc < 0 && errno == EINTR);

    if (rc == 0) {
        poll_interest_ = kPollReadable;
        if (!GlobalRuntime::Instance().poller().Add(fd_, id_, poll_interest_)) {
            if (error) *error = FormatErrorMessage(errno);
            return -1;
        }
        return 0;
    }

    if (errno != EINPROGRESS && errno != EWOULDBLOCK) {
        if (error) *error = FormatErrorMessage(errno);
        return -1;
    }

    poll_interest_ = kPollReadable | kPollWritable;
    if (!GlobalRuntime::Instance().poller().Add(fd_, id_, poll_interest_)) {
        if (error) *error = FormatErrorMessage(errno);
        return -1;
    }
    return 1;
}

bool ClientConnection::PlatformFinishConnect(std::string* error) {
    int error_code = 0;
    socklen_t len = sizeof(error_code);
    if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error_code, &len) != 0) {
        if (error) *error = FormatErrorMessage(errno);
        return false;
    }
    if (error_code != 0) {
        if (error) *error = FormatErrorMessage(error_code);
        return false;
    }
    return true;
}

bool ClientConnection::PlatformAfterConnected(std::string* error) {
    poll_interest_ = kPollReadable;
    if (!GlobalRuntime::Instance().poller().Modify(fd_, id_, poll_interest_)) {
        if (error) *error = FormatErrorMessage(errno);
        return false;
    }
    return true;
}

void ClientConnection::PlatformHandleReadable() {
    bool should_close = false;
    CloseReason close_reason = CloseReason::kRemoteClosed;
    std::string close_desc;
    size_t bytes_this_event = 0;

    while (bytes_this_event < kMaxReadBytesPerEvent) {
        ssize_t n = recv(fd_, recv_cache_, sizeof(recv_cache_), 0);
        if (n == 0) {
            should_close = true;
            close_reason = CloseReason::kRemoteClosed;
            break;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            should_close = true;
            close_reason = CloseReason::kError;
            close_desc = FormatErrorMessage(errno);
            break;
        }

        bytes_received_.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
        bytes_this_event += static_cast<size_t>(n);
        recv_buffer_.AddSlice(Slice(recv_cache_, static_cast<size_t>(n)));
    }
    if (recv_buffer_.GetBufferLength() > 0) {
        ParseStatus parse_status = ParseInput();
        if (parse_status == ParseStatus::kClosed) {
            return;
        }
        if (parse_status == ParseStatus::kYielded && should_close) {
            DeferCloseUntilInputDrained(close_reason, close_desc);
            return;
        }
    }
    if (should_close) {
        StartClose(close_reason, close_desc);
    }
}

void ClientConnection::PlatformHandleWritable() {
    size_t bytes_this_event = 0;
    while (bytes_this_event < kMaxWriteBytesPerEvent) {
        if (send_buffer_.Empty()) {
            UpdateWriteInterest();
            return;
        }
        Slice front = send_buffer_.Front();
        ssize_t n = SendNoSignal(fd_, front.data(), front.size());
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                UpdateWriteInterest();
                return;
            }
            StartClose(CloseReason::kError, FormatErrorMessage(errno));
            return;
        }
        if (n == 0) {
            UpdateWriteInterest();
            return;
        }

        size_t sent = static_cast<size_t>(n);
        bytes_sent_.fetch_add(sent, std::memory_order_relaxed);
        bytes_this_event += sent;
        pending_bytes_.fetch_sub(sent, std::memory_order_relaxed);
        send_buffer_.MoveHeader(sent);
    }
    UpdateWriteInterest();
}

void ClientConnection::PlatformCloseSocket() { CloseSocket(&fd_); }

bool ClientConnection::PlatformStartRecv(std::string* error) {
    (void)error;
    return true;
}

bool ClientConnection::PlatformStartSend(std::string* error) {
    int interest = kPollReadable;
    if (!send_buffer_.Empty()) {
        interest |= kPollWritable;
    }
    if (interest == poll_interest_) {
        return true;
    }
    if (!GlobalRuntime::Instance().poller().Modify(fd_, id_, interest)) {
        if (error) *error = FormatErrorMessage(errno);
        return false;
    }
    poll_interest_ = interest;
    return true;
}

void ClientConnection::HandleConnectCompletion(const PollEvent& ev) { (void)ev; }

void ClientConnection::HandleRecvCompletion(const PollEvent& ev) { (void)ev; }

void ClientConnection::HandleSendCompletion(const PollEvent& ev) { (void)ev; }

void ClientConnection::PlatformResetIoState() {}

} // namespace connx

#endif // _WIN32
