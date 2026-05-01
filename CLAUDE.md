# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```shell
# Shared library (default, RelWithDebInfo)
./build.sh

# Static library, with tests and examples
./build.sh -s --tests --examples

# Clean + Release build
./build.sh -c -t Release

# Manual cmake
mkdir build && cd build
cmake -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON ..
cmake --build . --parallel $(nproc)
```

Build outputs go to `build/`. The build script packages headers + libs into a `connx_<platform>_<arch>_v*.tar.gz`.

## Running Tests

```shell
# Build with tests, then run all:
./build.sh --tests
cd build && ctest

# Or run a single test binary directly:
./build/test_sync
./build/test_codec
./build/test_slice
./build/test_slice_buffer
./build/test_refcounted
./build/test_c_api
```

Tests use a custom lightweight framework defined in `test/testutil.h`. Each `test_*.cc` file becomes its own test binary. Test macros: `TEST(suite, name)`, `TEST_F(fixture, name)`, `ASSERT_EQ`, `ASSERT_TRUE`, `ASSERT_NE`, etc.

## Architecture

connx is a cross-platform (Linux epoll / macOS kqueue / Windows IOCP) client-side TCP networking library with pluggable protocol framing. C++11 core with a C ABI wrapper for FFI bindings. Zero external dependencies.

### Layer stack

```
Public API:   include/connx/c.h (C)    include/connx/client.h (C++)
                   │                           │
Bridge:       src/bridge/c.cc            src/bridge/client.cc
              (C opaque types wrap       (ClientAdaptor delegates
               C++ objects)               to ClientImpl)
                   │                           │
                   └───────────┬───────────────┘
                               │
Net:                src/net/clientimpl.h/cc
                    (connection state machine, MPSC event dispatch,
                     protocol parsing loop)
                    src/net/clientimpl_linux.cc  (epoll)
                    src/net/clientimpl_macos.cc  (kqueue)
                    src/net/clientimpl_win32.cc  (IOCP)
                    src/net/connect_timeout.cc
                    src/net/resolve_address.cc
                               │
Codec:              src/codec/*.cc
                    (Delimiter, FixedLength, LengthField, callback)
                               │
Utils:              src/utils/slice.h        (zero-copy string with SSO)
                    src/utils/slice_buffer.h (chain of slices as deque)
                    src/utils/mpscq.h        (lock-free MPSC queue)
                    src/utils/refcounted.h   (intrusive ref counting)
                    src/utils/log.h          (internal logging macros)
                    src/utils/status.h       (error type: shared_ptr<Status>)
Core:               src/core/library.h       (LibraryInitService base class)
```

### Key design patterns

**Two-thread model per client:** A platform-specific polling thread (epoll/kqueue/IOCP) does all I/O and posts `EventNode` structs onto a lock-free MPSC queue. A work thread consumes the queue and dispatches to `ClientHandler` callbacks serially. This keeps callbacks single-threaded per client.

**Ref counting for lifetime safety:** `ClientImpl` inherits `RefCounted<ClientImpl>`. The polling thread holds a ref while a socket is registered; releasing the last ref deletes the object. `ReleasePollRef()` atomically closes the fd, deregisters from epoll, and drops the poll reference.

**Slice/SliceBuffer for zero-copy I/O:** `Slice` is an SSO-like string (inline storage up to 23 bytes, heap beyond). `SliceBuffer` is a deque of Slices used for send and recv queues. The protocol parser operates on chained slices via `MergeFront()` to avoid copying.

**C API wrapping pattern:** C opaque types (`connx_client_t`, `connx_codec_t`, etc.) hold pointers to C++ objects. `src/bridge/c.cc` translates between the two. C callbacks are wrapped as C++ virtual method overrides (e.g., `connx_client_handler_s` implements `ClientHandler`).

**Error handling:** Errors use `connx_error` = `shared_ptr<Status>`. `CONNX_ERROR_NONE` is nullptr. Macros: `CONNX_POSIX_ERROR`, `CONNX_SYSTEM_ERROR`, `CONNX_ERROR_FROM_STATIC_STRING`.

**Logging:** Uses `CONNX_LOG_TRACE/DEBUG/INFO/WARN/ERROR` macros that check `connx_log_get_min_level()` before formatting. Default output is to stderr; redirectable via `connx_log_set_callback()`. Log filtering happens before the callback is invoked.

### What connx does NOT include

No reconnection, heartbeat, RPC, connection pooling, or TLS. These are application-layer concerns built on top.
