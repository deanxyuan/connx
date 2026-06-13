/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef _WIN32
#ifndef __APPLE__

#include "src/net/poller.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
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

uint32_t ToEpollEvents(int interest) {
    uint32_t events = EPOLLRDHUP;
    if (interest & kPollReadable) {
        events |= EPOLLIN;
    }
    if (interest & kPollWritable) {
        events |= EPOLLOUT;
    }
    return events;
}

void CloseFd(int* fd) {
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}
} // namespace

Poller::Poller()
    : poll_fd_(-1)
    , wake_fd_(-1) {}

Poller::~Poller() { Shutdown(); }

connx_error Poller::Init() {
    if (poll_fd_ >= 0) {
        return CONNX_ERROR_NONE;
    }

    poll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (poll_fd_ < 0) {
        poll_fd_ = epoll_create(kMaxEvents);
        if (poll_fd_ < 0) {
            return CONNX_POSIX_ERROR(errno, "epoll_create");
        }
        if (SetCloseOnExec(poll_fd_) != 0) {
            int err = errno;
            CloseFd(&poll_fd_);
            return CONNX_POSIX_ERROR(err, "fcntl(epoll)");
        }
    }

    wake_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wake_fd_ < 0) {
        int err = errno;
        CloseFd(&poll_fd_);
        return CONNX_POSIX_ERROR(err, "eventfd");
    }

    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.u64 = 0;
    if (epoll_ctl(poll_fd_, EPOLL_CTL_ADD, wake_fd_, &ev) != 0) {
        int err = errno;
        CloseFd(&wake_fd_);
        CloseFd(&poll_fd_);
        return CONNX_POSIX_ERROR(err, "epoll_ctl(wake)");
    }

    return CONNX_ERROR_NONE;
}

void Poller::Shutdown() {
    CloseFd(&wake_fd_);
    CloseFd(&poll_fd_);
}

bool Poller::Add(SocketHandle fd, ConnectionId id, int interest) {
    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = ToEpollEvents(interest);
    ev.data.u64 = id.ToUint64();
    return epoll_ctl(poll_fd_, EPOLL_CTL_ADD, fd, &ev) == 0;
}

bool Poller::Modify(SocketHandle fd, ConnectionId id, int interest) {
    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = ToEpollEvents(interest);
    ev.data.u64 = id.ToUint64();
    return epoll_ctl(poll_fd_, EPOLL_CTL_MOD, fd, &ev) == 0;
}

void Poller::Remove(SocketHandle fd) {
    if (fd < 0 || poll_fd_ < 0) {
        return;
    }
    epoll_ctl(poll_fd_, EPOLL_CTL_DEL, fd, nullptr);
}

void Poller::Wake() {
    if (wake_fd_ < 0) {
        return;
    }
    uint64_t one = 1;
    ssize_t unused = write(wake_fd_, &one, sizeof(one));
    (void)unused;
}

int Poller::Wait(int timeout_ms, std::vector<PollEvent>* events) {
    if (events == nullptr || poll_fd_ < 0) {
        return 0;
    }

    epoll_event evs[kMaxEvents];
    int n = epoll_wait(poll_fd_, evs, kMaxEvents, timeout_ms);
    if (n < 0) {
        if (errno == EINTR) {
            return 0;
        }
        return -1;
    }

    for (int i = 0; i < n; ++i) {
        if (evs[i].data.u64 == 0) {
            uint64_t value = 0;
            while (read(wake_fd_, &value, sizeof(value)) > 0) {
            }
            continue;
        }

        PollEvent out;
        out.id = ConnectionId::FromUint64(evs[i].data.u64);
        out.events = 0;
        out.error_code = 0;

        uint32_t flags = evs[i].events;
        if ((flags & EPOLLERR) || ((flags & EPOLLHUP) && !(flags & (EPOLLIN | EPOLLOUT)))) {
            out.events |= kPollEventError;
            out.error_code = ECONNRESET;
        }
        if (flags & (EPOLLIN | EPOLLRDHUP)) {
            out.events |= kPollEventReadable;
        }
        if (flags & EPOLLOUT) {
            out.events |= kPollEventWritable;
        }
        events->push_back(out);
    }
    return static_cast<int>(events->size());
}

} // namespace connx

#endif // __APPLE__
#endif // _WIN32
