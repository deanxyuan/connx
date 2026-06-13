/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifdef __APPLE__

#include "src/net/poller.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>

namespace connx {
namespace {
constexpr int kMaxEvents = 256;

int SetCloseOnExec(int fd) {
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

void CloseFd(int* fd) {
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

bool SetNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

int SocketError(int fd, const struct kevent& ev) {
    if (ev.flags & EV_ERROR) {
        return static_cast<int>(ev.data);
    }
    if ((ev.flags & EV_EOF) && ev.fflags != 0) {
        return static_cast<int>(ev.fflags);
    }

    int err = 0;
    socklen_t len = sizeof(err);
    if (fd >= 0 && getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err != 0) {
        return err;
    }
    return err;
}

} // namespace

Poller::Poller()
    : poll_fd_(-1)
    , wake_read_fd_(-1)
    , wake_write_fd_(-1) {}

Poller::~Poller() { Shutdown(); }

connx_error Poller::Init() {
    if (poll_fd_ >= 0) {
        return CONNX_ERROR_NONE;
    }

    poll_fd_ = kqueue();
    if (poll_fd_ < 0) {
        return CONNX_POSIX_ERROR(errno, "kqueue");
    }
    if (SetCloseOnExec(poll_fd_) != 0) {
        int err = errno;
        CloseFd(&poll_fd_);
        return CONNX_POSIX_ERROR(err, "fcntl(kqueue)");
    }

    int pipe_fds[2] = {-1, -1};
    if (pipe(pipe_fds) != 0) {
        int err = errno;
        CloseFd(&poll_fd_);
        return CONNX_POSIX_ERROR(err, "pipe");
    }
    wake_read_fd_ = pipe_fds[0];
    wake_write_fd_ = pipe_fds[1];
    if (!SetNonBlocking(wake_read_fd_) || !SetNonBlocking(wake_write_fd_) ||
        SetCloseOnExec(wake_read_fd_) != 0 || SetCloseOnExec(wake_write_fd_) != 0) {
        int err = errno;
        CloseFd(&wake_read_fd_);
        CloseFd(&wake_write_fd_);
        CloseFd(&poll_fd_);
        return CONNX_POSIX_ERROR(err, "fcntl(pipe)");
    }

    struct kevent ev;
    EV_SET(&ev, wake_read_fd_, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    if (kevent(poll_fd_, &ev, 1, nullptr, 0, nullptr) != 0) {
        int err = errno;
        CloseFd(&wake_read_fd_);
        CloseFd(&wake_write_fd_);
        CloseFd(&poll_fd_);
        return CONNX_POSIX_ERROR(err, "kevent(wake)");
    }
    return CONNX_ERROR_NONE;
}

void Poller::Shutdown() {
    CloseFd(&wake_write_fd_);
    CloseFd(&wake_read_fd_);
    CloseFd(&poll_fd_);
}

bool Poller::Add(SocketHandle fd, ConnectionId id, int interest) {
    struct kevent evs[2];
    int count = 0;
    intptr_t ident = static_cast<intptr_t>(fd);
    void* udata = reinterpret_cast<void*>(static_cast<uintptr_t>(id.ToUint64()));

    uint16_t read_flags = EV_ADD | ((interest & kPollReadable) ? EV_ENABLE : EV_DISABLE);
    EV_SET(&evs[count++], ident, EVFILT_READ, read_flags, 0, 0, udata);

    uint16_t write_flags = EV_ADD | ((interest & kPollWritable) ? EV_ENABLE : EV_DISABLE);
    EV_SET(&evs[count++], ident, EVFILT_WRITE, write_flags, 0, 0, udata);

    return kevent(poll_fd_, evs, count, nullptr, 0, nullptr) == 0;
}

bool Poller::Modify(SocketHandle fd, ConnectionId id, int interest) {
    return Add(fd, id, interest);
}

void Poller::Remove(SocketHandle fd) {
    if (fd < 0 || poll_fd_ < 0) {
        return;
    }
    struct kevent evs[2];
    EV_SET(&evs[0], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&evs[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    kevent(poll_fd_, evs, 2, nullptr, 0, nullptr);
}

void Poller::Wake() {
    if (wake_write_fd_ < 0) {
        return;
    }
    char one = 1;
    ssize_t unused = write(wake_write_fd_, &one, sizeof(one));
    (void)unused;
}

int Poller::Wait(int timeout_ms, std::vector<PollEvent>* events) {
    if (events == nullptr || poll_fd_ < 0) {
        return 0;
    }

    struct timespec ts;
    ts.tv_sec = timeout_ms / 1000;
    ts.tv_nsec = (timeout_ms % 1000) * 1000000L;

    struct kevent evs[kMaxEvents];
    int n = kevent(poll_fd_, nullptr, 0, evs, kMaxEvents, &ts);
    if (n < 0) {
        if (errno == EINTR) {
            return 0;
        }
        return -1;
    }

    for (int i = 0; i < n; ++i) {
        if (static_cast<int>(evs[i].ident) == wake_read_fd_) {
            char buf[64];
            while (read(wake_read_fd_, buf, sizeof(buf)) > 0) {
            }
            continue;
        }

        PollEvent out;
        out.id = ConnectionId::FromUint64(
            static_cast<uint64_t>(reinterpret_cast<uintptr_t>(evs[i].udata)));
        out.events = 0;
        out.error_code = 0;

        if ((evs[i].flags & EV_ERROR) ||
            ((evs[i].flags & EV_EOF) && evs[i].filter == EVFILT_WRITE)) {
            out.events |= kPollEventError;
            out.error_code = SocketError(static_cast<int>(evs[i].ident), evs[i]);
        }
        if (evs[i].filter == EVFILT_READ) {
            out.events |= kPollEventReadable;
        }
        if (evs[i].filter == EVFILT_WRITE) {
            out.events |= kPollEventWritable;
        }
        events->push_back(out);
    }
    return static_cast<int>(events->size());
}

} // namespace connx

#endif // __APPLE__
