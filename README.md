# connx

> Cross-platform client networking foundation - the connectivity base for your SDKs and applications.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C/C++](https://img.shields.io/badge/C%2FC%2B%2B-11-blue.svg)](https://isocpp.org/)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)]()

**connx** is a lightweight, cross-platform networking library written in C and C++. It provides the essential building blocks for client-side network connections - the "bottom layer" for applications that need to communicate with servers. With a clean C API at its core, connx enables seamless bindings to other languages such as Python, Java, and beyond.

## Features

- **Cross-platform I/O** - epoll (Linux), kqueue (macOS), IOCP (Windows)
- **C + C++ API** - Stable C ABI for FFI bindings; clean C++ virtual interface
- **Pluggable codec** - Delimiter / FixedLength / LengthField / custom callback
- **Connect timeout** - Configurable per-client deadline
- **Thread-safe** - Connect / Send / Disconnect callback from any thread
- **Zero dependencies** - C11 / C++11, no third-party libraries

**What connx does NOT do:** No reconnection, no heartbeat, no RPC, no connection pooling, no TLS. These are application-layer concerns - connx reports events via callbacks; your code decides what to do.

## Quick Example

Build and run the included echo examples:

```shell
mkdir build && cd build
cmake -DBUILD_EXAMPLES=ON ..
make

# Terminal 1: start echo server
./server 9000

# Terminal 2: C client
./client_c 127.0.0.1:9000

# Terminal 3: C++ client
./client_cc 127.0.0.1:9000
```

The same example flow applies on macOS; the standalone echo server includes the required SIGPIPE protection for Darwin builds.

Example source files:

| File                  | Description                                          |
| --------------------- | ---------------------------------------------------- |
| `example/server.cc`   | Simple echo server (standalone, no connx dependency) |
| `example/client_c.c`  | Echo client using connx C API                        |
| `example/client_cc.c` | Echo client using connx C++ API                      |

### Minimal C Usage

```c
#include <connx/c.h>

void on_message(void* ud, const void* data, size_t len) {
    // handle decode frame
}
int main() {
    connx_client_handler_t* h =
        connx_client_handler_new(NULL, on_connected, on_connect_failed, on_closed, on_message);

    connx_codec_t* codec = connx_codec_new_delimiter('\n');
    connx_client_options_t* opts = connx_client_options_new();
    connx_client_options_set_codec(opts, codec);

    connx_client_t* cli = connx_client_new(h, opts);
    connx_client_connect(cli, "127.0.0.1:8080");
    /* ... */
    connx_client_destroy(cli);
    connx_client_options_destroy(opts);
    connx_codec_destroy(codec);
    connx_client_handler_destroy(h);
    return 0;
}
```

### Minimal C++ Usage

```cpp
#include <connx/client.h>
#include <connx/codec/delimiter_codec.h>
#include <connx/options.h>
class MyHandler : public connx::ClientHandler {
    void OnConnected() override { /* send data*/ }
    void OnConnectFailed(const char* reason) override {}
    void OnClosed() override {}
    void OnMessage(const void* data, size_t len) override { /* handle frame */ };
};

int main() {
    connx::ClientOptions opts;
    opts.codec = new connx::DelimiterCodec('\n');

    MyHandler handler;
    connx::Client* cli = connx::CreateClient(&handler, opts);
    cli->Connect("127.0.0.1:8080");
    /* ... */
    connx::ReleaseClient(cli);
    return 0;
}
```

## Logging

connx has its own internal logging for debugging and monitoring. By default, all logs are written to `stderr`. You do **not** need to configure anything to use connx.

### Default Behavior (Zero Configuration)

```c
#include <connx/c.h>

int main() {
    // connx logs go to stderr automatically
    // [INFO] connecting to example.com:8080
    // [DEBUG] bytes sent: 1024
    connx_client_t* client = connx_client_new();
    connx_client_connect(client, "example.com:8080");
}
```

### Redirecting connx Logs to Your Own Logger

If you want to collect connx's internal logs (e.g., write to a file, send to a remote service, or integrate with your application's logging system), you can register a callback:

```c
#include <stdio.h>
#include <connx/c.h>

void my_log_handler(int level, int line, unsigned long threadid,
                    const char* msg, void* userdata) {
    FILE* fp = (FILE*)userdata;
    fprintf(fp, "[connx] %s\n", msg);
}

int main() {
    FILE* log_file = fopen("connx.log", "w");
    
    // Redirect connx logs to a file
    connx_log_set_callback(my_log_handler, log_file);
    
    // Now connx logs go to connx.log instead of stderr
    connx_client_t* client = connx_client_new();
    // ...
    
    // Remove callback to restore default stderr output
    connx_log_set_callback(NULL, NULL);
    fclose(log_file);
}
```

### Integration with C++ Logging Libraries

connx logs are independent from your application's logging. You can easily forward them to libraries like **spdlog**, **glog**, or **Boost.Log**:

```cpp
#include <spdlog/spdlog.h>
#include <connx/c.h>

void spdlog_bridge(int level, int line, unsigned long threadid,
                   const char* msg, void* userdata) {
    auto* logger = static_cast<spdlog::logger*>(userdata);
    
    // msg is already formatted by connx, just forward it
    switch (level) {
        case CONNX_LOG_LEVEL_ERROR: logger->error("{}", msg); break;
        case CONNX_LOG_LEVEL_WARN:  logger->warn("{}", msg);  break;
        case CONNX_LOG_LEVEL_INFO:  logger->info("{}", msg);  break;
        case CONNX_LOG_LEVEL_DEBUG: logger->debug("{}", msg); break;
        case CONNX_LOG_LEVEL_TRACE: logger->trace("{}", msg); break;
    }
}

int main() {
    auto app_logger = spdlog::rotating_logger_mt("app", "app.log", 
                                                  1024*1024*10, 3);
    
    // Your application uses spdlog directly (with compile-time formatting)
    app_logger->info("user {} connected from {}", user_id, ip);
    
    // Forward connx's internal logs to the same file
    connx_log_set_callback(spdlog_bridge, app_logger.get());
    
    connx_client_t* client = connx_client_new();
    // Both app and connx logs now go to app.log
}
```

### Important Notes

- **The callback receives pre-formatted strings.** `msg` is a null-terminated string, ready to use. You do NOT need to call `printf` or format it again.

- **Log level filtering happens before the callback.** If you set
`connx_log_set_min_level(CONNX_LOG_LEVEL_WARN)`, your callback will only receive WARN and ERROR messages.

- **The callback may be called from multiple threads.** Ensure your logging handler is thread-safe.

- **Lifecycle management is your responsibility.** If you pass a pointer via userdata, ensure it remains valid until the callback is removed by calling `connx_log_set_callback(NULL, NULL)`.

- **connx does NOT depend on any logging library.** The callback mechanism is pure C, with no external dependencies.

## Codec

| Codec                                      | Description                                       |
| ------------------------------------------ | ------------------------------------------------- |
| `connx_codec_new_delimiter('\n')`          | Split by delimiter character                      |
| `connx_codec_new_fixed_length(128)`        | Fixed-size frames                                 |
| `connx_codec_new_length_field(0, 4, 4, 1)` | Length-prefixed frames (4-byte big-endian header) |
| `connx_codec_new_callback(fn, ud)`         | Custom decode callback                            |

## Options

| Option             | Default        | Description                  |
| ------------------ | -------------- | ---------------------------- |
| `codec`            | required       | Protocol codec               |
| `connect_timeout`  | 0 (none)       | Connect timeout in ms        |
| `local_address`    | NULL (auto)    | Bind to specific local IP    |
| `tcp_nodelay`      | enable         | Disable Nagle's algorithm    |
| `send_buffer_size` | 0 (OS default) | Socket send buffer           |
| `recv_buffer_size` | 0 (OS default) | Socket recv buffer           |
| `linger`           | -1 (disabled)  | SO_LINGER timeout in seconds |

## Build

### Linux / macOS

```shell
mkdir build && cd build
cmake ..                              # shared library
cmake -DBUILD_STATIC=ON ..            # static library
cmake -DBUILD_TESTS=ON ..             # unit tests
cmake -DBUILD_EXAMPLES=ON ..          # example programs
make
sudo make install
```

Or use `build.sh`:

```shell
./build.sh                            # build shared library
./build.sh -s                         # build static library
./build.sh -c -t Release              # clean and rebuild in Release
./build.sh --tests --examples         # include tests and examples
```

### Windows (MSVC)

```shell
mkdir build && cd build
cmake -G "Visual Studio 16 2019" -A x64 ..
cmake --build . --config Release
cmake --install . --config Release
```

Or use the provided scripts: `build.bat` / `build.ps1`:

### Requirements

- CMake 3.14+
- C11 /C++11 compiler (GCC / Clang / MSVC)

## License

MIT License - see [LICENSE](LICENSE) for details.
