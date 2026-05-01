# connx macOS 平台移植设计方案

**版本**: 0.1.0
**日期**: 2026-04-28
**状态**: 草案

---

## 1. 目标

为 connx 库新增 macOS 平台支持，使用 kqueue（BSD 内核事件通知机制）作为 I/O 多路复用后端，与现有 Linux epoll 和 Windows IOCP 后端并列。

---

## 2. 现状分析

### 2.1 平台抽象架构

connx 采用「条件编译 + 独立编译单元」的方式隔离平台差异：

| 层级 | Linux (epoll) | Windows (IOCP) | 抽象方式 |
|------|---------------|-----------------|----------|
| 轮询后端 | `clientimpl_linux.cc` | `clientimpl_win32.cc` | CMakeLists.txt 按平台选择编译 |
| 类声明 | `clientimpl.h` (`#else` 分支) | `clientimpl.h` (`#ifdef _WIN32` 分支) | 预处理器宏 |
| 同步原语 | `sync.h` pthread 路径 | `sync.h` Win32 CRITICAL_SECTION 路径 | 预处理器宏 |
| 套接字头文件 | `sockaddr.h` POSIX 路径 | `sockaddr.h` Win32 路径 | 预处理器宏 |
| 错误处理 | `status.h` POSIX errno | `status.h` Win32 GetLastError | 预处理器宏 |
| 库初始化 | `init.cc` 空操作 | `init.cc` WSAStartup/WSACleanup | 预处理器宏 |

### 2.2 已有 POSIX 兼容性

macOS 天然兼容现有 `#else`（非 Win32）分支的大部分代码：

- **`clientimpl.h`**: `int fd_`、`void* PollingThread(void*)`、`void OnSendEvent()` 等签名无需修改
- **`sockaddr.h`**: POSIX 头文件（`sys/socket.h`、`netinet/in.h` 等）在 macOS 上均可用
- **`sync.h`**: pthread 互斥锁/条件变量在 macOS 上原生支持
- **`socket_util.cc`**: `inet_pton`、`fcntl` 等 POSIX API 在 macOS 上可用
- **`clientimpl.cc`**: 共享代码（协议解析、工作线程、MPSC 队列）完全平台无关

不需要修改的文件清单：`clientimpl.h`、`sockaddr.h`、`socket_util.h`、`socket_util.cc`、`sync.h`、`sync.cc`、`status.h`、`init.cc`、`clientimpl.cc`、`connect_timeout.cc`、`resolve_address.cc`，以及 `bridge/`、`codec/`、`core/`、`utils/` 下所有其他文件。

---

## 3. kqueue 与 epoll 技术对比

### 3.1 核心概念映射

| 概念 | epoll | kqueue |
|------|-------|--------|
| 创建实例 | `epoll_create(size)` | `kqueue()` |
| 注册/修改事件 | `epoll_ctl(EPOLL_CTL_ADD/MOD/DEL)` | `kevent()` + `EV_ADD`/`EV_ENABLE`/`EV_DISABLE`/`EV_DELETE` |
| 等待事件 | `epoll_wait()` | `kevent()` (统一系统调用) |
| 可读事件 | `EPOLLIN` | `EVFILT_READ` |
| 可写事件 | `EPOLLOUT` | `EVFILT_WRITE` |
| 边缘触发（仅读） | `EPOLLET` | `EV_CLEAR`（仅用于 `EVFILT_READ`） |
| 水平触发（写） | `EPOLLOUT`（默认水平触发） | `EVFILT_WRITE` 不加 `EV_CLEAR`（默认水平触发） |
| 错误事件 | `EPOLLERR` | `EV_ERROR` 标志 |
| 挂起事件 | `EPOLLHUP` | `EV_EOF` 标志（对端关闭） |
| 半关闭 | `EPOLLRDHUP` | `EV_EOF` 在 `EVFILT_READ` 上 |
| 用户数据指针 | `ev.data.ptr` | `ev.udata` |
| 超时参数 | 毫秒（`int`） | `struct timespec`（秒+纳秒，需转换） |

### 3.2 关键差异

1. **统一 API**: kqueue 使用同一个 `kevent()` 系统调用完成注册、修改、删除和等待；epoll 分为 `epoll_ctl()` 和 `epoll_wait()`。

2. **按过滤器独立控制**: kqueue 对每个 fd 的 EVFILT_READ 和 EVFILT_WRITE 是独立的内核过滤器，可单独启用/禁用。epoll 则使用组合的事件掩码。这意味着启用/禁用写通知时，kqueue 只需操作 EVFILT_WRITE，无需关心 EVFILT_READ。

3. **边缘触发仅用于读**: kqueue 的 `EV_CLEAR` 与 epoll 的 `EPOLLET` 语义等价——事件被用户态取出后清除状态，需要循环读取/写入直到返回 EAGAIN。但 connx 对 **读** 使用边缘触发，对 **写** 使用水平触发（与 epoll 后端一致：epoll 的 `EPOLLOUT` 默认即为水平触发，仅 `EPOLLIN` 附加了 `EPOLLET`）。原因见下文第 4.1.3 节设计说明。

4. **EOF 处理**: kqueue 没有独立的对端关闭事件；当对端关闭连接时，`EVFILT_READ` 过滤器返回 `EV_EOF` 标志，此时 socket 缓冲区中可能仍有未读取的数据。

5. **无需显式删除**: 当 fd 被 `close()` 时，kqueue 自动清理该 fd 上的所有过滤器。epoll 在 `close()` 之前需要 `EPOLL_CTL_DEL`（但 connx Linux 后端在实际 `close()` 之前也会调用 `epoll_ctl(DEL)`）。

6. **超时参数类型不同**: `kevent()` 的超时参数是 `const struct timespec*`（秒+纳秒），而 `epoll_wait()` 接受毫秒为单位的 `int`。需要在 `KqueueWait` 内部做转换：`timeout_ms` → `{timeout_ms / 1000, (timeout_ms % 1000) * 1000000}`。

---

## 4. 设计方案

### 4.1 新增文件

#### `src/net/clientimpl_macos.cc`（约 500 行）

新建 kqueue 轮询后端，结构与 `clientimpl_linux.cc` 镜像对称。

##### 4.1.1 全局状态与 kqueue 辅助函数

```cpp
#define MAX_KQUEUE_EVENTS 256
static struct kevent g_kevents[MAX_KQUEUE_EVENTS];
static int g_kqueue_fd = -1;
static bool g_kqueue_running = false;
static pthread_t g_kqueue_thread = 0;

// 初始化 kqueue 实例，设置 CLOEXEC
static connx_error KqueueInit();

// 注册 EVFILT_READ（边缘触发，EV_CLEAR）和 EVFILT_WRITE（水平触发，不加 EV_CLEAR）
static int KqueueRegister(int fd, void* udata);

// 启用 EVFILT_WRITE 通知（send buffer 有数据待发送时调用）
static int KqueueEnableWrite(int fd, void* udata);

// 禁用 EVFILT_WRITE 通知（send buffer 为空时调用，避免忙轮询）
static int KqueueDisableWrite(int fd, void* udata);

// 删除 fd 上的所有过滤器（在 close 之前调用）
static int KqueueDelete(int fd);

// 等待事件，timeout_ms 为毫秒（内部转换为 struct timespec）
static int KqueueWait(int timeout_ms);
```

##### 4.1.2 轮询线程

```cpp
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

            // 错误检查：EV_ERROR 或 EOF 表示连接异常
            if (ev->flags & EV_ERROR || ev->flags & EV_EOF) {
                int err = ev->flags & EV_ERROR ? static_cast<int>(ev->data) : 0;
                connector->OnErrorEvent(internal::kErrorEvent, err);
                connector->Unref();
                continue;
            }

            // connect 完成后首次事件到达
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

**EV_EOF 处理决策**：在轮询线程中检测 `EV_EOF` 并直接视为错误事件。理由是 connx 不支持半关闭语义（`EPOLLRDHUP` 在 Linux 后端中也直接导致 `OnErrorEvent`），统一处理可简化逻辑。如果 `EV_EOF` 伴随 `EVFILT_READ` 且缓冲区有数据，在 `OnErrorEvent` → `ReleasePollRef` → `close()` 流程中数据会丢失，但这一行为与 Linux 后端一致（`EPOLLRDHUP` 时同样不会额外读取）。

##### 4.1.3 事件注册与修改

**写通知采用水平触发（level-triggered）的设计理由**：

epoll 后端对 `EPOLLOUT` 使用默认的水平触发（仅 `EPOLLIN` 附加了 `EPOLLET`），因此 `OnSendEvent` / `OnRecvEvent` 每次都会通过 `EpollModify` 在 `EPOLLIN` 和 `EPOLLIN|EPOLLOUT` 之间切换来控制是否接收写通知。kqueue 后端不应在 `EVFILT_WRITE` 上使用 `EV_CLEAR`，原因有二：

1. **语义一致**：保持与 epoll 后端相同的水平触发写语义，避免边缘触发下需要手动 re-arm 的复杂性。
2. **避免丢事件**：边缘触发 + 未排空缓冲区时，仅靠"保持 EVFILT_WRITE 启用"不会重新触发通知——需要 socket 经历 writable→not-writable→writable 状态转换。水平触发则会在每次 `kevent()` 调用时持续返回可写事件，直到显式禁用。

读方向仍使用 `EV_CLEAR`（边缘触发），与 epoll 的 `EPOLLET` 对应。

**KqueueRegister 实现**：
```cpp
static int KqueueRegister(int fd, void* udata) {
    struct kevent ev[2];
    EV_SET(&ev[0], fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, udata);
    EV_SET(&ev[1], fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, udata);  // 水平触发
    return kevent(g_kqueue_fd, ev, 2, NULL, 0, NULL);
}
```

**KqueueEnableWrite / KqueueDisableWrite 实现**：
```cpp
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
```

`ConnectImpl` 中的注册：
```cpp
// 创建非阻塞 socket，发起 connect，注册到 kqueue
Ref();
fd_ = sock_fd;
poll_registered_.store(true, std::memory_order_release);
if (KqueueRegister(sock_fd, this) != 0) {
    fd_ = -1;
    poll_registered_.store(false, std::memory_order_release);
    Unref();
    ::close(sock_fd);
    return CONNX_SYSTEM_ERROR(errno, "kevent register");
}
```

`OnSendEvent` / `OnRecvEvent` 中的写事件控制：
```cpp
void ClientImpl::OnSendEvent() {
    std::unique_lock<std::mutex> g(smtx_);
    if (!send_buffer_.Empty() && SendImpl() != 0) {
        OnErrorEvent(internal::kSendEvent, errno);
        return;
    }
    if (send_buffer_.Empty()) {
        KqueueDisableWrite(fd_, this);   // 无数据待发，禁用写通知避免忙轮询
    }
    // 缓冲区非空时：EVFILT_WRITE 保持启用（水平触发），下次 kevent() 会持续通知
}

void ClientImpl::OnRecvEvent() {
    if (RecvImpl() != 0) {
        OnErrorEvent(internal::kRecvEvent, errno);
        return;
    }
    // RecvImpl → TransferData → ParsingProtocol → ClientHandler 回调可能产生响应数据
    // 需根据当前 send buffer 状态管理写通知
    if (send_buffer_.Empty()) {
        KqueueDisableWrite(fd_, this);
    }
    // 否则 EVFILT_WRITE 已启用（水平触发），持续通知直到缓冲区排空
}
```

`SendMsg` 中重新启用写通知：
```cpp
bool ClientImpl::SendMsg(const Slice& msg) {
    if (shutdown_ || msg.empty()) return false;
    std::unique_lock<std::mutex> g(smtx_);
    send_buffer_.AddSlice(msg);
    KqueueEnableWrite(fd_, this);   // 水平触发：若 socket 可写则立即通知
    return true;
}
```

##### 4.1.4 套接字工具函数

`connx_set_socket_nonblocking`、`connx_set_socket_cloexec`、`connx_set_socket_low_latency`、`connx_prepare_client_socket`、`connx_create_client_socket` 与 Linux 版本完全一致（POSIX API）。从 `clientimpl_linux.cc` 复制即可。

##### 4.1.5 SIGPIPE 防护

macOS 上向已关闭的 socket 写入会触发 `SIGPIPE`（默认终止进程）。需要在 `connx_prepare_client_socket` 中增加：

```cpp
#ifdef SO_NOSIGPIPE
    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val));
#endif
```

`SO_NOSIGPIPE` 是 macOS/BSD 特有选项，Linux 上不存在此宏，使用 `#ifdef` 条件编译即可。

---

### 4.2 修改文件

#### 4.2.1 `CMakeLists.txt`

**当前状态**：三路分支（`if(WIN32)` / `elseif(APPLE)` / `else()`）已存在，`clientimpl_macos.cc` 已在各分支的 `list(REMOVE_ITEM)` 中被正确排除/保留。macOS 特有的 `CMAKE_OSX_DEPLOYMENT_TARGET`（第 21 行）和 `MACOSX_RPATH` 设置（第 114-118 行）也已就位。

**需确认的检查项**：

1. 源文件排除逻辑是否与当前代码一致：
```cmake
if(WIN32)
    list(REMOVE_ITEM NET_SOURCE
        ${PROJECT_SOURCE_DIR}/src/net/clientimpl_linux.cc
        ${PROJECT_SOURCE_DIR}/src/net/clientimpl_macos.cc)
elseif(APPLE)
    list(REMOVE_ITEM NET_SOURCE
        ${PROJECT_SOURCE_DIR}/src/net/clientimpl_linux.cc
        ${PROJECT_SOURCE_DIR}/src/net/clientimpl_win32.cc)
else()
    list(REMOVE_ITEM NET_SOURCE
        ${PROJECT_SOURCE_DIR}/src/net/clientimpl_win32.cc
        ${PROJECT_SOURCE_DIR}/src/net/clientimpl_macos.cc)
endif()
```

2. `BASE_LIBRARIES` 无需修改。macOS 上 `-lpthread` 和 `-lm` 为系统自带，链接器静默接受这些参数。

#### 4.2.2 `build.sh`（第 107-130 行）

**问题**：打包逻辑仅处理 `.so` 共享库，macOS 使用 `.dylib` 格式；包名固定为 `connx_linux_v*`。

**修改方案：**

```bash
# 检测操作系统
os_name=$(uname -s)
case "$os_name" in
    Darwin)  platform="darwin";  lib_ext="dylib" ;;
    Linux)   platform="linux";   lib_ext="so" ;;
    *)       platform=$(echo "$os_name" | tr '[:upper:]' '[:lower:]'); lib_ext="so" ;;
esac

# 查找共享库时兼容 .so 和 .dylib
real_lib=$(find "$current_build_path" -maxdepth 1 \( -name "libconnx.so.*.*" -o -name "libconnx.*.dylib" \) ! -type l -print -quit)

# 包名使用检测到的平台
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
```

**注意**：macOS 上 CMake 生成的共享库命名为 `libconnx.X.Y.Z.dylib`，与 Linux 的 `libconnx.so.X.Y.Z` 不同。符号链接处理也需要调整。如使用 `BUILD_STATIC=ON`（仅构建静态库），则无此问题。

---

### 4.3 无需修改的文件

| 文件 | 原因 |
|------|------|
| `clientimpl.h` | `#ifdef _WIN32` / `#else` 分支定义了 macOS 所需的正确声明（`int fd_`, `void* PollingThread`, `void OnSendEvent()` 等） |
| `sockaddr.h` | POSIX 头文件路径在 macOS 上完全可用 |
| `socket_util.cc/h` | 使用标准 POSIX socket API，跨 Linux/macOS 兼容 |
| `sync.h/cc` | pthread 实现直接用于 macOS |
| `status.h` | `MakeStatusFromPosixError` 使用 `errno`，macOS 兼容 |
| `init.cc` | 非 Win32 路径为空操作，macOS 无需等效 WSAStartup |
| `clientimpl.cc` | 协议解析、工作线程、事件分发逻辑完全平台无关 |
| 其他（bridge/codec/core/utils） | 全部平台无关 |

---

## 5. 实现步骤

### 第一步：创建 `src/net/clientimpl_macos.cc`

以 `clientimpl_linux.cc` 为模板，将以下部分替换为 kqueue 等效实现：

| Linux (epoll) | macOS (kqueue) |
|---------------|----------------|
| `#include <sys/epoll.h>` | `#include <sys/event.h>` |
| `g_epoll_fd`, `g_epoll_events` | `g_kqueue_fd`, `g_kevents` |
| `EpollInit()` | `KqueueInit()` |
| `EpollAdd(fd, this, EPOLLIN\|EPOLLOUT\|EPOLLET\|EPOLLRDHUP)` | `KqueueRegister(fd, this)`（读=EV_CLEAR，写=水平触发） |
| `EpollModify(fd, this, events)` | `KqueueEnableWrite(fd, this)` / `KqueueDisableWrite(fd, this)` |
| `EpollDelete(fd, events)` | `KqueueDelete(fd)` |
| `EpollWait(MIN_TIME_SLICE)` | `KqueueWait(MIN_TIME_SLICE)`（内部 `timeout_ms` → `struct timespec`） |
| `ev->events & (EPOLLERR\|EPOLLHUP\|EPOLLRDHUP)` | `ev->flags & (EV_ERROR\|EV_EOF)` |
| `ev->events & EPOLLIN` | `ev->filter == EVFILT_READ` |
| `ev->events & EPOLLOUT` | `ev->filter == EVFILT_WRITE` |
| `CLOSE_SOCKET(fd)` 使用 `::close()` | 相同（POSIX） |
| `connx_prepare_client_socket` | 相同，增加 `SO_NOSIGPIPE` |

### 第二步：确认 `CMakeLists.txt`

当前 CMakeLists.txt 已包含 `elseif(APPLE)` 三路分支和 macOS 特有设置。确认 `clientimpl_macos.cc` 创建后能被 `aux_source_directory` 正确采集，且各分支的 `list(REMOVE_ITEM)` 排除逻辑正确即可。

### 第三步：修改 `build.sh`

适配 macOS 共享库格式（`.dylib`）和平台命名。

### 第四步：编译验证

```shell
mkdir build && cd build
cmake -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON ..
cmake --build . --parallel $(sysctl -n hw.logicalcpu)
ctest
```

### 第五步：集成测试

在 macOS 上运行 `test_integration`，验证 epoll 的 echo server 与 kqueue 客户端的端到端通信。

---

## 6. 可选优化（不阻塞移植，建议后续实施）

### 6.1 提取公共 POSIX 套接字工具

当前 `connx_set_socket_nonblocking`、`connx_set_socket_cloexec`、`connx_set_socket_low_latency`、`connx_prepare_client_socket`、`connx_create_client_socket` 在 `clientimpl_linux.cc` 中定义，需完整复制到 `clientimpl_macos.cc`（约 120 行）。

建议提取到 `src/net/socket_util_posix.cc`（新文件），两个 POSIX 后端共同引用，消除重复。CMakeLists.txt 中在非 Win32 路径下加入该文件。

### 6.2 统一构建变量名

当前 `BASE_LIBRARIES` 在 Linux 上包含 `m` 和 `pthread`，在 macOS 上实际不需要（系统隐式链接）。可改为：

```cmake
if(APPLE)
    set(BASE_LIBRARIES)
elseif(UNIX)
    set(BASE_LIBRARIES m pthread)
endif()
```

影响极小，属于代码清洁度优化。

---

## 7. 风险评估

| 风险项 | 等级 | 缓解措施 |
|--------|------|----------|
| EV_CLEAR 仅用于读方向 | 低 | 写方向使用水平触发，与 epoll 后端的 EPOLLOUT 语义一致，避免边缘触发 re-arm 复杂性 |
| SIGPIPE 信号导致进程终止 | 中 | 在 socket 设置中添加 `SO_NOSIGPIPE` |
| macOS 缺少 TCP_QUICKACK | 无 | 已通过 `#ifdef TCP_QUICKACK` 条件编译处理 |
| 共享库格式差异 (.so vs .dylib) | 低 | build.sh 中添加格式检测；仅影响打包步骤 |
| kevent EV_EOF 时机与 epoll EPOLLRDHUP 差异 | 低 | 两者都在对端关闭时触发，connx 均通过 OnErrorEvent → close 处理 |

---

## 8. 总结

macOS 移植的核心工作是新增一个约 500 行的 `clientimpl_macos.cc`（kqueue 后端），以及对 CMakeLists.txt 和 build.sh 做小幅平台适配。得益于现有 `#ifdef _WIN32` / `#else` 的架构设计，`clientimpl.h` 及所有工具类代码无需任何改动。整体移植风险低，预计工作量约半天。
