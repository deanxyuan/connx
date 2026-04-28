/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 *
 * Minimal single-threaded echo server for integration testing.
 * No connx dependency — uses raw platform sockets.
 */

#ifndef CONNX_TEST_SUPPORT_ECHOSERVER_H_
#define CONNX_TEST_SUPPORT_ECHOSERVER_H_

#include <atomic>
#include <string>
#include <thread>

namespace test {

class EchoServer {
public:
    EchoServer();
    ~EchoServer();

    // Start listening on the given port. 0 = OS picks a free port.
    // Returns the actual port number, or -1 on error.
    int Start(int port = 0);

    // Stop the server and join the accept/echo thread.
    void Stop();

    int Port() const { return port_; }

private:
    void Run();

#ifdef _WIN32
    // On Windows WSAStartup must be called once per process.  We track whether
    // this instance performed the startup so Stop() can balance it.
    bool wsa_initialized_;
    void* listen_fd_; // SOCKET
#else
    int listen_fd_;
#endif
    int port_;
    std::atomic<bool> running_;
    std::thread thread_;
};

} // namespace test

#endif // CONNX_TEST_SUPPORT_ECHOSERVER_H_
