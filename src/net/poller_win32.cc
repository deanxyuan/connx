/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifdef _WIN32

#include "src/net/poller.h"

#include <map>
#include <memory>
#include <mutex>

#include "src/utils/string.h"

namespace connx {
namespace {

constexpr size_t kMaxCompletions = 256;

struct PollerState {
    PollerState()
        : iocp(NULL)
        , connectex(nullptr) {}

    HANDLE iocp;
    LPFN_CONNECTEX connectex;
    std::mutex mtx;
    std::map<OVERLAPPED*, WinIoRequest*> requests;
};

PollerState* AsState(void* state) { return static_cast<PollerState*>(state); }

bool LoadConnectEx(PollerState* state, SocketHandle fd, std::string* error) {
    {
        std::lock_guard<std::mutex> lock(state->mtx);
        if (state->connectex != nullptr) {
            return true;
        }
    }

    GUID guid = WSAID_CONNECTEX;
    DWORD bytes = 0;
    LPFN_CONNECTEX fn = nullptr;
    if (WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &fn,
                 sizeof(fn), &bytes, nullptr, nullptr) != 0) {
        if (error) *error = FormatErrorMessage(WSAGetLastError());
        return false;
    }

    std::lock_guard<std::mutex> lock(state->mtx);
    if (state->connectex == nullptr) {
        state->connectex = fn;
    }
    return true;
}

void TrackRequest(PollerState* state, WinIoRequest* req) {
    std::lock_guard<std::mutex> lock(state->mtx);
    state->requests[&req->overlapped] = req;
}

std::shared_ptr<WinIoRequest> UntrackRequest(PollerState* state, OVERLAPPED* overlapped) {
    std::lock_guard<std::mutex> lock(state->mtx);
    std::map<OVERLAPPED*, WinIoRequest*>::iterator it = state->requests.find(overlapped);
    if (it == state->requests.end()) {
        return std::shared_ptr<WinIoRequest>();
    }
    std::shared_ptr<WinIoRequest> req(it->second);
    state->requests.erase(it);
    return req;
}

WinIoRequest* RemoveRequest(PollerState* state, OVERLAPPED* overlapped) {
    std::lock_guard<std::mutex> lock(state->mtx);
    std::map<OVERLAPPED*, WinIoRequest*>::iterator it = state->requests.find(overlapped);
    if (it == state->requests.end()) {
        return nullptr;
    }
    WinIoRequest* req = it->second;
    state->requests.erase(it);
    return req;
}

bool PollOne(PollerState* state, DWORD timeout_ms, PollEvent* ev) {
    DWORD bytes = 0;
    ULONG_PTR key = 0;
    OVERLAPPED* overlapped = nullptr;
    BOOL ok = GetQueuedCompletionStatus(state->iocp, &bytes, &key, &overlapped, timeout_ms);
    if (overlapped == nullptr) {
        if (!ok && GetLastError() == WAIT_TIMEOUT) {
            return false;
        }
        return false;
    }

    std::shared_ptr<WinIoRequest> req = UntrackRequest(state, overlapped);
    if (!req) {
        return false;
    }

    ev->id = req->id;
    ev->operation = req->operation;
    ev->bytes_transferred = static_cast<size_t>(bytes);
    ev->data = req;

    if (!ok) {
        ev->events = kPollEventError;
        ev->error_code = static_cast<int>(GetLastError());
    } else if (req->operation == kPollOpConnect || req->operation == kPollOpSend) {
        ev->events = kPollEventWritable;
    } else if (req->operation == kPollOpRecv) {
        ev->events = kPollEventReadable;
    }
    return true;
}

} // namespace

Poller::Poller()
    : state_(nullptr) {}

Poller::~Poller() { Shutdown(); }

connx_error Poller::Init() {
    if (state_ != nullptr) {
        return CONNX_ERROR_NONE;
    }

    std::unique_ptr<PollerState> state(new PollerState);
    state->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (state->iocp == NULL) {
        return CONNX_SYSTEM_ERROR(GetLastError(), "CreateIoCompletionPort");
    }

    state_ = state.release();
    return CONNX_ERROR_NONE;
}

void Poller::Shutdown() {
    PollerState* state = AsState(state_);
    if (state == nullptr) {
        return;
    }
    state_ = nullptr;
    if (state->iocp != NULL) {
        CloseHandle(state->iocp);
        state->iocp = NULL;
    }
    delete state;
}

bool Poller::Add(SocketHandle fd, ConnectionId id, int interest) {
    (void)interest;
    PollerState* state = AsState(state_);
    if (state == nullptr || !IsValidSocketHandle(fd)) {
        return false;
    }
    HANDLE h = CreateIoCompletionPort(reinterpret_cast<HANDLE>(fd), state->iocp,
                                      static_cast<ULONG_PTR>(id.ToUint64()), 0);
    return h != NULL;
}

bool Poller::Modify(SocketHandle fd, ConnectionId id, int interest) {
    (void)fd;
    (void)id;
    (void)interest;
    return state_ != nullptr;
}

void Poller::Remove(SocketHandle fd) {
    if (IsValidSocketHandle(fd)) {
        CancelIoEx(reinterpret_cast<HANDLE>(fd), nullptr);
    }
}

void Poller::Wake() {
    PollerState* state = AsState(state_);
    if (state == nullptr || state->iocp == NULL) {
        return;
    }
    PostQueuedCompletionStatus(state->iocp, 0, 0, nullptr);
}

int Poller::Wait(int timeout_ms, std::vector<PollEvent>* events) {
    PollerState* state = AsState(state_);
    if (events == nullptr || state == nullptr || state->iocp == NULL) {
        return 0;
    }

    PollEvent ev;
    if (!PollOne(state, static_cast<DWORD>(timeout_ms), &ev)) {
        return 0;
    }
    events->push_back(ev);

    while (events->size() < kMaxCompletions) {
        PollEvent extra;
        if (!PollOne(state, 0, &extra)) {
            break;
        }
        events->push_back(extra);
    }
    return static_cast<int>(events->size());
}

bool Poller::StartConnect(SocketHandle fd, ConnectionId id, const connx_sockaddr* addr,
                          int addr_len, std::string* error) {
    PollerState* state = AsState(state_);
    if (state == nullptr || !IsValidSocketHandle(fd)) {
        if (error) *error = "poller is not initialized";
        return false;
    }
    if (!LoadConnectEx(state, fd, error)) {
        return false;
    }

    WinIoRequest* req = new WinIoRequest(kPollOpConnect);
    req->id = id;
    TrackRequest(state, req);

    BOOL ok = state->connectex(fd, addr, addr_len, nullptr, 0, nullptr, &req->overlapped);
    if (!ok) {
        int err = WSAGetLastError();
        if (err != ERROR_IO_PENDING && err != WSA_IO_PENDING) {
            delete RemoveRequest(state, &req->overlapped);
            if (error) *error = FormatErrorMessage(err);
            return false;
        }
    }
    return true;
}

bool Poller::StartRecv(SocketHandle fd, ConnectionId id, size_t len, std::string* error) {
    PollerState* state = AsState(state_);
    if (state == nullptr || !IsValidSocketHandle(fd)) {
        if (error) *error = "poller is not initialized";
        return false;
    }

    WinIoRequest* req = new WinIoRequest(kPollOpRecv);
    req->id = id;
    req->buffer = MakeSliceByLength(len);
    req->wsa_buffer.buf = req->buffer.buffer();
    req->wsa_buffer.len = static_cast<ULONG>(req->buffer.size());

    DWORD flags = 0;
    TrackRequest(state, req);
    int rc = WSARecv(fd, &req->wsa_buffer, 1, nullptr, &flags, &req->overlapped, nullptr);
    if (rc == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            delete RemoveRequest(state, &req->overlapped);
            if (error) *error = FormatErrorMessage(err);
            return false;
        }
    }
    return true;
}

bool Poller::StartSend(SocketHandle fd, ConnectionId id, const Slice& data, std::string* error) {
    PollerState* state = AsState(state_);
    if (state == nullptr || !IsValidSocketHandle(fd)) {
        if (error) *error = "poller is not initialized";
        return false;
    }

    WinIoRequest* req = new WinIoRequest(kPollOpSend);
    req->id = id;
    req->buffer = data;
    req->wsa_buffer.buf = req->buffer.buffer();
    req->wsa_buffer.len = static_cast<ULONG>(req->buffer.size());

    TrackRequest(state, req);
    int rc = WSASend(fd, &req->wsa_buffer, 1, nullptr, 0, &req->overlapped, nullptr);
    if (rc == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            delete RemoveRequest(state, &req->overlapped);
            if (error) *error = FormatErrorMessage(err);
            return false;
        }
    }
    return true;
}

} // namespace connx

#endif // _WIN32
