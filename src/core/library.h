/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_CORE_LIBRARY_H_
#define CONNX_SRC_CORE_LIBRARY_H_

namespace connx {

class LibraryInitService {
public:
    LibraryInitService(bool call_init = true);
    virtual ~LibraryInitService();

    LibraryInitService(const LibraryInitService&) = delete;
    LibraryInitService& operator=(const LibraryInitService&) = delete;

    bool initialized() const { return init_called_; }

private:
    bool init_called_;
};
} // namespace connx

#endif // CONNX_SRC_CORE_LIBRARY_H_
