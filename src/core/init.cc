/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "connx/client.h"
// Make sure winsock2.h is included before windows.h
#include "src/net/sockaddr.h" // winsock2.h
#include "src/utils/sync.h"   // windows.h
#include "src/utils/time.h"
#include "src/utils/useful.h"
#include "src/net/clientimpl.h"

namespace connx {

#ifdef _WIN32
static int winsock_init() {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data);
}
static void winsock_shutdown() { WSACleanup(); }
#endif

static connx_mutex_t g_init_mtx;
static int g_initializations = 0;
static connx_once_t g_basic_init = CONNX_ONCE_INIT;
static void do_basic_init() {
    ConnxMutexInit(&g_init_mtx);
    g_initializations = 0;
    connx_time_init();
}
void LibraryInit() {
    ConnxOnceInit(&g_basic_init, do_basic_init);
    MutexLock lock(&g_init_mtx);
    if (++g_initializations == 1) {
#ifdef _WIN32
        CONNX_ASSERT(winsock_init() == 0 && "winsock init failure.");
#endif
        ClientImpl::Init();
    }
}

void LibraryShutdown() {
    MutexLock lock(&g_init_mtx);
    if (--g_initializations == 0) {
        ClientImpl::Shutdown();
#ifdef _WIN32
        winsock_shutdown();
#endif
    }
}
} // namespace connx
