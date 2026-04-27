/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "src/core/library.h"
#include <assert.h>
#include <atomic>
#include <mutex>
#include <memory>
#include "src/utils/useful.h"
#include "connx/client.h"

namespace connx {
static LibraryInterface* g_lib = nullptr;
namespace internal {
void ConnxLibrary::Init() { LibraryInit(); }

void ConnxLibrary::Shutdown() { LibraryShutdown(); }
LibraryInitializer::LibraryInitializer() {
    if (connx::g_lib == nullptr) {
        static std::unique_ptr<ConnxLibrary> glib(new ConnxLibrary);
        connx::g_lib = glib.get();
    }
}

int LibraryInitializer::summon() { return 0; }
} // namespace internal
LibraryInitService::LibraryInitService(bool call_init)
    : init_called_(call_init) {
    if (call_init) {
        CONNX_ASSERT(g_lib && "connx library not initialized.");
        g_lib->Init();
    }
}
LibraryInitService::~LibraryInitService() {
    if (init_called_) {
        CONNX_ASSERT(g_lib && "connx library not initialized.");
        g_lib->Shutdown();
    }
}
} // namespace connx
