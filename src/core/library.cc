/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "src/core/library.h"

#include "connx/client.h"

namespace connx {
LibraryInitService::LibraryInitService(bool call_init)
    : init_called_(false) {
    if (call_init) {
        init_called_ = LibraryInit();
    }
}

LibraryInitService::~LibraryInitService() {
    if (init_called_) {
        LibraryShutdown();
    }
}
} // namespace connx
