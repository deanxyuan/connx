/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifdef _WIN32

#include "src/net/client_connection.h"

#include <mswsock.h>
#include <string.h>

#include "src/net/runtime.h"
#include "src/net/socket_util.h"
#include "src/utils/string.h"

namespace connx {
namespace {

std::string WinSockErrorString(int err) { return FormatErrorMessage(err); }

void CloseSocket(SocketHandle* fd) {
    if (IsValidSocketHandle(*fd)) {
        closesocket(*fd);
        *fd = InvalidSocketHandle();
    }
}

bool SetNonBlocking(SocketHandle fd, std::string* error) {
    u_long non_blocking = 1;
    if (ioctlsocket(fd, FIONBIO, &non_blocking) != 0) {
        if (error) *error = WinSockErrorString(WSAGetLastError());
        return false;
    }
    return true;
}

bool ApplyTcpOptions(SocketHandle fd, const TcpOptions& tcp, std::string* error) {
    if (!SetNonBlocking(fd, error)) {
        return false;
    }
    if (tcp.tcp_nodelay) {
        int val = 1;
        if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&val),
                       sizeof(val)) != 0) {
            if (error) *error = WinSockErrorString(WSAGetLastError());
            return false;
        }
    }
    if (tcp.send_buffer_size > 0) {
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
                   reinterpret_cast<const char*>(&tcp.send_buffer_size),
                   sizeof(tcp.send_buffer_size));
    }
    if (tcp.recv_buffer_size > 0) {
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
                   reinterpret_cast<const char*>(&tcp.recv_buffer_size),
                   sizeof(tcp.recv_buffer_size));
    }
    if (tcp.linger_sec >= 0) {
        struct linger ling;
        ling.l_onoff = (tcp.linger_sec > 0) ? 1 : 0;
        ling.l_linger = static_cast<u_short>(tcp.linger_sec);
        setsockopt(fd, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&ling),
                   sizeof(ling));
    }
    return true;
}

void MakeWildcardAddress(int family, connx_resolved_address* out) {
    memset(out, 0, sizeof(*out));
    if (family == AF_INET6) {
        connx_sockaddr_in6* addr = reinterpret_cast<connx_sockaddr_in6*>(out->addr);
        addr->sin6_family = AF_INET6;
        addr->sin6_port = htons(0);
        out->len = sizeof(connx_sockaddr_in6);
        return;
    }
    connx_sockaddr_in* addr = reinterpret_cast<connx_sockaddr_in*>(out->addr);
    addr->sin_family = AF_INET;
    addr->sin_port = htons(0);
    out->len = sizeof(connx_sockaddr_in);
}

} // namespace

bool ClientConnection::PlatformOpenSocket(const connx_resolved_address& addr,
                                          SocketHandle* out_fd, std::string* error) {
    const connx_sockaddr* sa = reinterpret_cast<const connx_sockaddr*>(addr.addr);
    SocketHandle fd = WSASocket(sa->sa_family, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
                                WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (!IsValidSocketHandle(fd)) {
        if (error) *error = WinSockErrorString(WSAGetLastError());
        return false;
    }

    if (!ApplyTcpOptions(fd, opts_.tcp, error)) {
        CloseSocket(&fd);
        return false;
    }
    connx_resolved_address local_addr;
    if (opts_.local_address != nullptr) {
        if (!connx_string_to_sockaddr(&local_addr, opts_.local_address, 0)) {
            if (error) *error = "invalid local address";
            CloseSocket(&fd);
            return false;
        }
    } else {
        MakeWildcardAddress(sa->sa_family, &local_addr);
    }

    if (bind(fd, reinterpret_cast<const connx_sockaddr*>(local_addr.addr),
             static_cast<int>(local_addr.len)) != 0) {
        if (error) *error = WinSockErrorString(WSAGetLastError());
        CloseSocket(&fd);
        return false;
    }

    *out_fd = fd;
    return true;
}

int ClientConnection::PlatformConnect(SocketHandle fd, const connx_resolved_address& addr,
                                      std::string* error) {
    if (!GlobalRuntime::Instance().poller().Add(fd, id_, 0)) {
        if (error) *error = WinSockErrorString(GetLastError());
        return -1;
    }

    if (!GlobalRuntime::Instance().poller().StartConnect(
            fd, id_, reinterpret_cast<const connx_sockaddr*>(addr.addr),
            static_cast<int>(addr.len), error)) {
        return -1;
    }
    return 1;
}

bool ClientConnection::PlatformFinishConnect(std::string* error) {
    (void)error;
    return true;
}

bool ClientConnection::PlatformAfterConnected(std::string* error) {
    if (setsockopt(fd_, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0) != 0) {
        if (error) *error = WinSockErrorString(WSAGetLastError());
        return false;
    }
    return PlatformStartRecv(error);
}

void ClientConnection::PlatformHandleReadable() {}

void ClientConnection::PlatformHandleWritable() {}

void ClientConnection::PlatformCloseSocket() { CloseSocket(&fd_); }

bool ClientConnection::PlatformStartRecv(std::string* error) {
    if (recv_pending_ || !id_.valid() || !IsValidSocketHandle(fd_)) {
        return true;
    }
    recv_pending_ = true;
    if (!GlobalRuntime::Instance().poller().StartRecv(fd_, id_, sizeof(recv_cache_), error)) {
        recv_pending_ = false;
        return false;
    }
    return true;
}

bool ClientConnection::PlatformStartSend(std::string* error) {
    if (send_pending_ || send_buffer_.Empty() || !id_.valid() || !IsValidSocketHandle(fd_)) {
        return true;
    }

    Slice front = send_buffer_.Front();
    Slice copy = front;
    send_pending_ = true;
    if (!GlobalRuntime::Instance().poller().StartSend(fd_, id_, copy, error)) {
        send_pending_ = false;
        return false;
    }
    return true;
}

void ClientConnection::HandleConnectCompletion(const PollEvent& ev) {
    if (ev.events & kPollEventError) {
        StartClose(CloseReason::kConnectFailed, FormatErrorMessage(ev.error_code));
        return;
    }
    HandleConnected();
}

void ClientConnection::HandleRecvCompletion(const PollEvent& ev) {
    recv_pending_ = false;
    if (!IsConnected()) {
        return;
    }
    if (ev.events & kPollEventError) {
        StartClose(CloseReason::kError, FormatErrorMessage(ev.error_code));
        return;
    }
    if (ev.bytes_transferred == 0) {
        if (recv_buffer_.GetBufferLength() > 0) {
            DeferCloseUntilInputDrained(CloseReason::kRemoteClosed, "");
            if (!parse_continuation_pending_) {
                ParseInput();
            }
            return;
        }
        StartClose(CloseReason::kRemoteClosed, "");
        return;
    }

    std::shared_ptr<WinIoRequest> req =
        std::static_pointer_cast<WinIoRequest>(ev.data);
    if (!req || req->buffer.size() < ev.bytes_transferred) {
        StartClose(CloseReason::kError, "invalid recv completion");
        return;
    }

    bytes_received_.fetch_add(static_cast<uint64_t>(ev.bytes_transferred),
                              std::memory_order_relaxed);
    recv_buffer_.AddSlice(Slice(req->buffer.data(), ev.bytes_transferred));
    if (ParseInput() == ParseStatus::kClosed) {
        return;
    }

    std::string error;
    if (!PlatformStartRecv(&error)) {
        StartClose(CloseReason::kError, error);
    }
}

void ClientConnection::HandleSendCompletion(const PollEvent& ev) {
    send_pending_ = false;
    if (!IsConnected()) {
        return;
    }
    if (ev.events & kPollEventError) {
        StartClose(CloseReason::kError, FormatErrorMessage(ev.error_code));
        return;
    }

    if (ev.bytes_transferred > 0) {
        size_t sent = ev.bytes_transferred;
        bytes_sent_.fetch_add(sent, std::memory_order_relaxed);
        pending_bytes_.fetch_sub(sent, std::memory_order_relaxed);
        if (!send_buffer_.MoveHeader(sent)) {
            StartClose(CloseReason::kError, "invalid send completion");
            return;
        }
    }

    std::string error;
    if (!PlatformStartSend(&error)) {
        StartClose(CloseReason::kError, error);
    }
}

void ClientConnection::PlatformResetIoState() {
    recv_pending_ = false;
    send_pending_ = false;
}

} // namespace connx

#endif // _WIN32
