/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_TEST_SUPPORT_ECHOSERVER_H_
#define CONNX_TEST_SUPPORT_ECHOSERVER_H_

#include <string>

namespace test {

// Start the echo server on the given port (0 = OS picks a free port).
// Returns the actual port number, or -1 on error.
int StartEchoServer(int port = 0);

// Start a one-shot server that sends payload after accept and closes the peer.
// Reuse StopEchoServer() to join the server thread.
int StartBurstCloseServer(const std::string& payload, int port = 0);

// Stop the server and join the I/O thread.
void StopEchoServer();

// Return the port the server is listening on (valid after StartEchoServer).
int EchoServerPort();

} // namespace test

#endif // CONNX_TEST_SUPPORT_ECHOSERVER_H_
