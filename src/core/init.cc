/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "connx/client.h"
// Make sure winsock2.h is included before windows.h
#include "src/net/sockaddr.h" // winsock2.h
#include "src/utils/sync.h"   // windows.h
#include "src/utils/time.h"
#include "src/utils/log.h"
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
bool LibraryInit() {
    ConnxOnceInit(&g_basic_init, do_basic_init);
    MutexLock lock(&g_init_mtx);
    if (++g_initializations == 1) {
#ifdef _WIN32
        int rc = winsock_init();
        if (rc != 0) {
            --g_initializations;
            CONNX_LOG_ERROR("connx winsock init failed error=%d", rc);
            return false;
        }
#endif
        connx_error err = ClientImpl::Init();
        if (err != CONNX_ERROR_NONE) {
#ifdef _WIN32
            winsock_shutdown();
#endif
            --g_initializations;
            CONNX_LOG_ERROR("connx runtime init failed: %s", err->ToString().c_str());
            return false;
        }
    }
    return true;
}

void LibraryShutdown() {
    ConnxOnceInit(&g_basic_init, do_basic_init);
    MutexLock lock(&g_init_mtx);
    if (g_initializations <= 0) {
        return;
    }
    if (--g_initializations == 0) {
        ClientImpl::Shutdown();
#ifdef _WIN32
        winsock_shutdown();
#endif
    }
}
} // namespace connx
