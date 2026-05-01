# connx macOS 平台移植实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 connx 库新增 macOS 平台支持，使用 kqueue 作为 I/O 多路复用后端。

**Architecture:** 新增 `clientimpl_macos.cc`（kqueue 后端），与现有 `clientimpl_linux.cc`（epoll）和 `clientimpl_win32.cc`（IOCP）并列。CMakeLists.txt 已预置三路分支，无需修改。`build.sh` 需适配 `.dylib` 共享库格式和平台命名。读方向使用 `EV_CLEAR` 边缘触发，写方向使用水平触发（与 epoll 后端的 EPOLLOUT 语义一致）。

**Tech Stack:** C++11, kqueue/kevent, pthread, POSIX socket API

**预计总工作量:** 约 4-5 小时

---

## 文件变更总览

| 操作 | 文件 | 说明 |
|------|------|------|
| 新增 | `src/net/clientimpl_macos.cc` | kqueue 轮询后端，约 510 行 |
| 修改 | `build.sh` | 平台检测 + .dylib 打包 |
| 确认 | `CMakeLists.txt` | APPLE 分支已存在，无需修改 |

---

### Task 1: 创建文件骨架——头文件、全局状态、LibraryInitializer

**文件:**
- Create: `src/net/clientimpl_macos.cc`

- [ ] **Step 1: 创建文件前缀（版权、include、全局变量）**

写入 `src/net/clientimpl_macos.cc`：

```cpp
/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "src/net/clientimpl.h"
#include "connx/codec.h"
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include "src/net/connect_timeout.h"
#include "src/net/socket_util.h"
#include "src/utils/status.h"
#include "src/utils/string.h"
#include "src/utils/time.h"
#include "src/utils/useful.h"

static connx::internal::LibraryInitializer g_lib_initializer;
static connx::ConnectTimeoutList g_connect_timeouts;

#define MAX_KQUEUE_EVENTS 256
static struct kevent g_kevents[MAX_KQUEUE_EVENTS];
static int g_kqueue_fd = -1;
static bool g_kqueue_running = false;
static pthread_t g_kqueue_thread = 0;

#define CLOSE_SOCKET(fd)                                                                           \
    if (fd != -1) {                                                                                \
        ::close(fd);                                                                               \
        fd = -1;                                                                                   \
    }

namespace connx {
// 后续任务将在此处添加 ClientImpl 方法实现
} // namespace connx
```

> **说明**：include 列表与 `clientimpl_linux.cc` 一致，仅将 `<sys/epoll.h>` 替换为 `<sys/event.h>`。全局变量命名从 `g_epoll_*` 改为 `g_kqueue_*`。`CLOSE_SOCKET` 宏与 Linux 版本完全相同。

---

### Task 2: 添加 socket 工具函数

**文件:**
- Modify: `src/net/clientimpl_macos.cc` — 在 `namespace connx {` 之前插入

- [ ] **Step 1: 添加 socket 工具函数（从 clientimpl_linux.cc 复制，增加 SO_NOSIGPIPE）**

在 `#define CLOSE_SOCKET(fd) ...` 之后、`namespace connx {` 之前插入：

```cpp
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
    int status = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
    return status == 0 ? CONNX_ERROR_NONE : CONNX_POSIX_ERROR(errno, "setsockopt(TCP_NODELAY)");
}

connx_error connx_prepare_client_socket(int fd, const connx::TcpOptions& tcp) {
    connx_error ret = CONNX_ERROR_NONE;

    ret = connx_set_socket_nonblocking(fd, 1);
    if (ret != CONNX_ERROR_NONE) goto failure;

    ret = connx_set_socket_cloexec(fd, 1);
    if (ret != CONNX_ERROR_NONE) goto failure;

#ifdef SO_NOSIGPIPE
    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val));
#endif

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
```

> **差异点 vs Linux 版本**：`connx_prepare_client_socket` 在 `connx_set_socket_cloexec` 之后增加了 `#ifdef SO_NOSIGPIPE` 块（第 4 个函数体内，`goto failure` 标签之前）。其余 5 个函数与 Linux 版本完全一致。

---

### Task 3: 添加 kqueue 辅助函数

**文件:**
- Modify: `src/net/clientimpl_macos.cc` — 在 `namespace connx {` 之前、`connx_create_client_socket` 之后插入

- [ ] **Step 1: 添加 6 个 kqueue 辅助函数**

```cpp
static connx_error KqueueInit() {
    g_kqueue_fd = kqueue();
    if (g_kqueue_fd < 0) {
        return CONNX_SYSTEM_ERROR(errno, "kqueue");
    }
    if (fcntl(g_kqueue_fd, F_SETFD, FD_CLOEXEC) != 0) {
        return CONNX_SYSTEM_ERROR(errno, "fcntl");
    }
    return CONNX_ERROR_NONE;
}

static int KqueueRegister(int fd, void* udata) {
    struct kevent ev[2];
    EV_SET(&ev[0], fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, udata);
    EV_SET(&ev[1], fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, udata);
    return kevent(g_kqueue_fd, ev, 2, NULL, 0, NULL);
}

static int KqueueEnableWrite(int fd, void* udata) {
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_WRITE, EV_ENABLE, 0, 0, udata);
    return kevent(g_kqueue_fd, &ev, 1, NULL, 0, NULL);
}

static int KqueueDisableWrite(int fd, void* udata) {
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_WRITE, EV_DISABLE, 0, 0, udata);
    return kevent(g_kqueue_fd, &ev, 1, NULL, 0, NULL);
}

static int KqueueDelete(int fd) {
    struct kevent ev[2];
    EV_SET(&ev[0], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    EV_SET(&ev[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    return kevent(g_kqueue_fd, ev, 2, NULL, 0, NULL);
}

static int KqueueWait(int timeout_ms) {
    struct timespec ts;
    ts.tv_sec = timeout_ms / 1000;
    ts.tv_nsec = (timeout_ms % 1000) * 1000000;
    return kevent(g_kqueue_fd, NULL, 0, g_kevents, MAX_KQUEUE_EVENTS, &ts);
}
```

> **关键设计决策**：
> - `KqueueRegister`：读=EV_CLEAR（边缘触发），写=水平触发（不加 EV_CLEAR）
> - `KqueueEnableWrite`/`KqueueDisableWrite`：仅操作 EVFILT_WRITE，EVFILT_READ 状态不受影响
> - `KqueueDelete`：删除 fd 上的两个过滤器。udata 传 NULL（filter 已足够定位）
> - `KqueueWait`：内部将毫秒转为 `struct timespec`

---

### Task 4: 添加 PollingThread、Init、Shutdown、ClearConnectDeadline

**文件:**
- Modify: `src/net/clientimpl_macos.cc` — 在 `namespace connx {` 内部添加

- [ ] **Step 1: 在 `namespace connx {` 内部添加轮询线程和生命周期管理方法**

```cpp
connx_error ClientImpl::Init() {
    connx_error r = KqueueInit();
    if (r != CONNX_ERROR_NONE) {
        return r;
    }

    g_kqueue_running = true;

    if (!g_kqueue_thread) {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
        int result = pthread_create(&g_kqueue_thread, &attr, ClientImpl::PollingThread, NULL);
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
    g_kqueue_running = false;

    CLOSE_SOCKET(g_kqueue_fd);

    if (g_kqueue_thread) {
        pthread_join(g_kqueue_thread, NULL);
        g_kqueue_thread = 0;
    }
}

void* ClientImpl::PollingThread(void*) {
    while (g_kqueue_running) {

        int count = KqueueWait(MIN_TIME_SLICE);

        for (int i = 0; i < count; i++) {
            struct kevent* ev = &g_kevents[i];
            ClientImpl* connector = reinterpret_cast<ClientImpl*>(ev->udata);

            if (!connector) continue;
            connector->Ref();

            if (connector->shutdown_) {
                connector->Unref();
                continue;
            }

            if (ev->flags & EV_ERROR || ev->flags & EV_EOF) {
                int err = ev->flags & EV_ERROR ? static_cast<int>(ev->data) : 0;
                connector->OnErrorEvent(internal::kErrorEvent, err);
                connector->Unref();
                continue;
            }

            if (!connector->is_connected_) {
                connector->last_recv_time_ = GetCurrentMillisec();
                connector->last_send_time_ = connector->last_recv_time_;
                connector->OnConnected();
            }

            if (ev->filter == EVFILT_READ) {
                connector->OnRecvEvent();
            }
            if (ev->filter == EVFILT_WRITE) {
                connector->OnSendEvent();
            }
            connector->Unref();
        }
        g_connect_timeouts.CheckTimeouts();
    }
    return NULL;
}
```

> **对比 Linux 版本的关键差异**：
> - `ev->flags & (EV_ERROR || EV_EOF)` 替代 `ev->events & (EPOLLERR|EPOLLHUP|EPOLLRDHUP)`
> - `ev->filter == EVFILT_READ/EVFILT_WRITE` 替代 `ev->events & EPOLLIN/EPOLLOUT`
> - 错误码从 `ev->data`（kqueue 在 EV_ERROR 时将 errno 放入 data 字段）
> - shutdown 时关闭 `g_kqueue_fd`（触发 `kevent()` 返回 EBADF，轮询线程退出）
> - `EV_EOF` 直接视为错误，与 Linux 后端 `EPOLLRDHUP` 处理一致

---

### Task 5: 添加 ClientImpl 构造/析构、Start、Stop

**文件:**
- Modify: `src/net/clientimpl_macos.cc` — 在 `namespace connx {` 内部、PollingThread 之后添加

- [ ] **Step 1: 添加构造/析构、Start、Stop**

```cpp
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
    if (opt_.codec) delete opt_.codec;
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
        KqueueDelete(fd_);
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
```

> **与 Linux 版本的唯一差异**：`Stop()` 中 `KqueueDelete(fd_)` 替代 `EpollDelete(fd_, EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP)`，kqueue 版本不需要事件掩码参数。

---

### Task 6: 添加 ConnectImpl、Disconnect、OnErrorEvent、ReleasePollRef

**文件:**
- Modify: `src/net/clientimpl_macos.cc` — 在 Stop() 之后添加

- [ ] **Step 1: 添加连接管理和错误处理方法**

```cpp
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
        g_connect_timeouts.Register(this);
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
    fd_ = sock_fd;
    poll_registered_.store(true, std::memory_order_release);
    if (KqueueRegister(sock_fd, this) != 0) {
        ret = CONNX_SYSTEM_ERROR(errno, "kevent register");
        fd_ = -1;
        poll_registered_.store(false, std::memory_order_release);
        Unref();
        ::close(sock_fd);
        return ret;
    }

    return CONNX_ERROR_NONE;
}

void ClientImpl::Disconnect() {
    if (fd_ == -1 || !is_connected_) return;
    KqueueDelete(fd_);
    ReleasePollRef();
}

void ClientImpl::OnErrorEvent(int event_type, int err) {
    ClearConnectDeadline(this);

    if (!ReleasePollRef()) {
        return;
    }

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
```

> **与 Linux 版本的差异**：
> - `ConnectImpl`：`KqueueRegister` 替代 `EpollAdd`，失败路径完整回滚（`fd_ = -1`, `Unref()`, `::close()`）
> - `Disconnect`：`KqueueDelete(fd_)` 替代 `EpollDelete(fd_, ...)`
> - `OnErrorEvent`、`ReleasePollRef`：完全一致

---

### Task 7: 添加 I/O 方法——SendMsg、OnSendEvent、OnRecvEvent、SendImpl、RecvImpl

**文件:**
- Modify: `src/net/clientimpl_macos.cc` — 在 ReleasePollRef() 之后添加

- [ ] **Step 1: 添加 I/O 方法**

```cpp
bool ClientImpl::SendMsg(const Slice& msg) {
    if (shutdown_ || msg.empty()) {
        return false;
    }
    std::unique_lock<std::mutex> g(smtx_);
    send_buffer_.AddSlice(msg);
    KqueueEnableWrite(fd_, this);
    return true;
}
bool ClientImpl::SendMsg(Slice&& msg) {
    if (shutdown_ || msg.empty()) {
        return false;
    }
    std::unique_lock<std::mutex> g(smtx_);
    send_buffer_.AddSlice(std::move(msg));
    KqueueEnableWrite(fd_, this);
    return true;
}

void ClientImpl::OnSendEvent() {
    std::unique_lock<std::mutex> g(smtx_);
    if (!send_buffer_.Empty() && SendImpl() != 0) {
        OnErrorEvent(internal::kSendEvent, errno);
        return;
    }
    if (send_buffer_.Empty()) {
        KqueueDisableWrite(fd_, this);
    }
}

void ClientImpl::OnRecvEvent() {
    if (RecvImpl() != 0) {
        OnErrorEvent(internal::kRecvEvent, errno);
        return;
    }
    if (send_buffer_.Empty()) {
        KqueueDisableWrite(fd_, this);
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
```

> **与 Linux 版本的关键差异**：
> - `SendMsg`（两个重载）：`KqueueEnableWrite` 替代 `EpollModify(EPOLLIN|EPOLLOUT|EPOLLET)`
> - `OnSendEvent`：缓冲区空时 `KqueueDisableWrite` 替代 `EpollModify(EPOLLIN|EPOLLET)`；缓冲区非空时无需操作（写方向为水平触发，下次 `kevent()` 会自动通知）
> - `OnRecvEvent`：同上，无 mutex 锁（与 Linux 版本行为一致）
> - `SendImpl`、`RecvImpl`：完全一致（纯 POSIX API）

---

### Task 8: 修改 build.sh——平台检测与 .dylib 打包

**文件:**
- Modify: `build.sh:108-132`（打包逻辑区块）

- [ ] **Step 1: 替换打包逻辑**

将 `build.sh` 第 99-148 行（打包区块）替换为：

```bash
# Package
if [ -n "$version" ]; then
    # Detect platform
    os_name=$(uname -s)
    case "$os_name" in
        Darwin)  platform="darwin" ;;
        Linux)   platform="linux" ;;
        *)       platform=$(echo "$os_name" | tr '[:upper:]' '[:lower:]') ;;
    esac

    # Create package directories
    mkdir -p "$current_build_path/lib"
    mkdir -p "$current_build_path/include"

    # Copy headers
    cp -r "$project_source_dir/include/connx" "$current_build_path/include/"

    # Copy libraries (handle both .so and .dylib)
    if [ "$platform" = "darwin" ]; then
        real_lib=$(find "$current_build_path" -maxdepth 1 -name 'libconnx.*.dylib' ! -type l -print -quit)
        if [ -n "$real_lib" ]; then
            lib_basename=$(basename "$real_lib")
            cp "$real_lib" "$current_build_path/lib/"
            major=$(echo "$version" | cut -d. -f1)
            ln -sf "$lib_basename" "$current_build_path/lib/libconnx.$major.dylib"
            ln -sf "$lib_basename" "$current_build_path/lib/libconnx.dylib"
        fi
    else
        real_lib=$(find "$current_build_path" -maxdepth 1 -name 'libconnx.so.*.*' ! -type l -print -quit)
        if [ -n "$real_lib" ]; then
            so_basename=$(basename "$real_lib")
            cp "$real_lib" "$current_build_path/lib/"
            major=$(echo "$version" | cut -d. -f1)
            ln -sf "$so_basename" "$current_build_path/lib/libconnx.so.$major"
            ln -sf "$so_basename" "$current_build_path/lib/libconnx.so"
        fi
    fi

    # Fallback: static library
    if [ -z "$(ls -A "$current_build_path/lib/" 2>/dev/null)" ]; then
        cp "$current_build_path"/libconnx.a "$current_build_path/lib/" 2>/dev/null
    fi

    # Create archive
    cd "$current_build_path" || exit 1
    arch=$(uname -m)
    case "$arch" in
        aarch64|arm64)
            package_name="connx_${platform}_arm64_v${version}.tar.gz"
            ;;
        x86_64|amd64)
            package_name="connx_${platform}_amd64_v${version}.tar.gz"
            ;;
        *)
            package_name="connx_${platform}_${arch}_v${version}.tar.gz"
            ;;
    esac

    tar -zcf "$package_name" include lib

    # Determine output directory
    output_dir="${output_dir:-$project_source_dir/output}"
    [[ "$output_dir" != /* ]] && output_dir="$project_source_dir/$output_dir"
    mkdir -p "$output_dir"

    cp "$package_name" "$output_dir/"
    echo "packed: $output_dir/$package_name"

    # Cleanup temporary files in build directory
    rm -f "$package_name"
    rm -rf "$current_build_path/include" "$current_build_path/lib"
    echo "---- finish ----"
fi
```

> **变更说明**：
> 1. 新增 `uname -s` 平台检测（Darwin → darwin, Linux → linux）
> 2. 共享库查找分两路：darwin 用 `libconnx.*.dylib`，其他用 `libconnx.so.*.*`
> 3. 符号链接处理也分两路：`.dylib` 和 `.so` 命名规则不同
> 4. 包名从硬编码 `connx_linux_v*` 改为 `connx_${platform}_${arch}_v*`
> 5. 静态库回退逻辑保留

---

### Task 9: 确认 CMakeLists.txt 无需修改

**文件:**
- 确认: `CMakeLists.txt`

- [ ] **Step 1: 验证 CMakeLists.txt APPLE 分支配置正确**

在项目根目录运行以下检查：

```shell
# 确认 elseif(APPLE) 分支存在，且 clientimpl_macos.cc 排除逻辑正确
grep -n 'APPLE\|clientimpl_macos' CMakeLists.txt
```

预期输出应包含：
```
21:	if (APPLE)
46:	elseif(APPLE)
48:	    list(REMOVE_ITEM NET_SOURCE ${PROJECT_SOURCE_DIR}/src/net/clientimpl_macos.cc)
51:	    list(REMOVE_ITEM NET_SOURCE ${PROJECT_SOURCE_DIR}/src/net/clientimpl_win32.cc)
    list(REMOVE_ITEM NET_SOURCE ${PROJECT_SOURCE_DIR}/src/net/clientimpl_macos.cc)
114:	if(APPLE)
```

> **说明**：`CMAKE_OSX_DEPLOYMENT_TARGET`（第 21 行）、三路分支（第 43-52 行）、`MACOSX_RPATH`（第 114-118 行）均已预置，无需修改。创建 `clientimpl_macos.cc` 后，`aux_source_directory` 会自动采集该文件，再经 `list(REMOVE_ITEM)` 按平台过滤。

---

### Task 10: 编译和测试验证

- [ ] **Step 1: 在 macOS 上编译**

```shell
cd /path/to/connx
mkdir build && cd build
cmake -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON ..
cmake --build . --parallel $(sysctl -n hw.logicalcpu)
```

预期：编译成功，无错误。编译输出中应包含 `clientimpl_macos.cc` 的编译命令。

- [ ] **Step 2: 运行单元测试**

```shell
cd build
ctest --output-on-failure
```

预期：所有测试通过。重点关注的测试：
- `test_sync` — 同步原语
- `test_codec` — 协议编解码
- `test_slice` / `test_slice_buffer` — 零拷贝缓冲区
- `test_refcounted` — 引用计数生命周期
- `test_c_api` — C API 包装

- [ ] **Step 3: 运行集成测试**

```shell
cd build
./test_integration
```

预期：PASS。该测试启动 echo server（epoll），connx 客户端（kqueue）连接、发包、收包、验证回显。

- [ ] **Step 4: 验证打包**

```shell
cd /path/to/connx
./build.sh -c -j $(sysctl -n hw.logicalcpu)
```

预期输出：`packed: .../output/connx_darwin_arm64_vX.Y.Z.tar.gz`（Apple Silicon）或 `connx_darwin_amd64_vX.Y.Z.tar.gz`（Intel Mac）。

---

### Task 11: 提交

- [ ] **Step 1: 提交所有变更**

```bash
git add src/net/clientimpl_macos.cc build.sh
git commit -m "$(cat <<'EOF'
feat: add macOS support with kqueue backend

Add clientimpl_macos.cc using kqueue/kevent as the I/O multiplexing
backend. Read direction uses EV_CLEAR (edge-triggered), write direction
uses level-triggered to match epoll's EPOLLOUT semantics.

Update build.sh with platform detection (.dylib vs .so) and package
naming (connx_darwin_*).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## 注意事项

1. **无法在 Linux 上编译验证**：kqueue 是 BSD/macOS 特有 API，`clientimpl_macos.cc` 只能在 macOS 上编译。在 Linux 上开发时，CMakeLists.txt 的三路分支会排除该文件，不影响 Linux 构建。

2. **SO_NOSIGPIPE 的 `#ifdef` 保护**：该宏仅在 macOS/BSD 上定义，Linux 上不会触发，因此 socket 工具函数在两个平台共用时无需额外处理（但当前是复制而非共用，见 Task 2）。

3. **EV_EOF 和 EPOLLRDHUP 的行为差异**：kqueue 的 `EV_EOF` 在 `EVFILT_READ` 上触发时，socket 缓冲区可能仍有未读数据。当前设计与 Linux 后端一致（直接视为错误），如果未来需要优雅关闭语义，需要额外处理。
