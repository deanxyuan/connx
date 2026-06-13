/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_NET_POLLER_H_
#define CONNX_SRC_NET_POLLER_H_

#include <memory>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <string>
#include <vector>

#include "src/net/connection_id.h"
#include "src/net/sockaddr.h"
#include "src/utils/slice.h"
#include "src/utils/status.h"

namespace connx {

#ifdef _WIN32
typedef SOCKET SocketHandle;
#else
typedef int SocketHandle;
#endif

static inline SocketHandle InvalidSocketHandle() {
#ifdef _WIN32
    return INVALID_SOCKET;
#else
    return -1;
#endif
}

static inline bool IsValidSocketHandle(SocketHandle fd) {
#ifdef _WIN32
    return fd != INVALID_SOCKET;
#else
    return fd >= 0;
#endif
}

enum PollInterest {
    kPollReadable = 1 << 0,
    kPollWritable = 1 << 1,
};

enum PollEventType {
    kPollEventReadable = 1 << 0,
    kPollEventWritable = 1 << 1,
    kPollEventError = 1 << 2,
};

enum PollOperation {
    kPollOpReady = 0,
    kPollOpConnect = 1,
    kPollOpRecv = 2,
    kPollOpSend = 3,
};

#ifdef _WIN32
struct WinIoRequest {
    explicit WinIoRequest(int op)
        : operation(op)
        , id()
        , wsa_buffer() {
        memset(&overlapped, 0, sizeof(overlapped));
    }

    OVERLAPPED overlapped;
    int operation;
    ConnectionId id;
    Slice buffer;
    WSABUF wsa_buffer;
};
#endif

struct PollEvent {
    PollEvent()
        : id()
        , events(0)
        , error_code(0)
        , operation(kPollOpReady)
        , bytes_transferred(0) {}

    ConnectionId id;
    int events;
    int error_code;
    int operation;
    size_t bytes_transferred;
    std::shared_ptr<void> data;
};

class Poller {
public:
    Poller();
    ~Poller();

    connx_error Init();
    void Shutdown();
    bool Add(SocketHandle fd, ConnectionId id, int interest);
    bool Modify(SocketHandle fd, ConnectionId id, int interest);
    void Remove(SocketHandle fd);
    void Wake();
    int Wait(int timeout_ms, std::vector<PollEvent>* events);

#ifdef _WIN32
    bool StartConnect(SocketHandle fd, ConnectionId id, const connx_sockaddr* addr,
                      int addr_len, std::string* error);
    bool StartRecv(SocketHandle fd, ConnectionId id, size_t len, std::string* error);
    bool StartSend(SocketHandle fd, ConnectionId id, const Slice& data, std::string* error);
#endif

private:
    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;

#ifdef _WIN32
    void* state_;
#elif defined(__APPLE__)
    int poll_fd_;
    int wake_read_fd_;
    int wake_write_fd_;
#else
    int poll_fd_;
    int wake_fd_;
#endif
};

} // namespace connx

#endif // CONNX_SRC_NET_POLLER_H_
