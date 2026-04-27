/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_CORE_LIBRARY_H_
#define CONNX_SRC_CORE_LIBRARY_H_

namespace connx {
class LibraryInterface {
public:
    virtual ~LibraryInterface() = default;
    virtual void Init() = 0;
    virtual void Shutdown() = 0;
};

class LibraryInitService {
public:
    LibraryInitService(bool call_init = true);
    virtual ~LibraryInitService();

private:
    bool init_called_;
};
namespace internal {
class ConnxLibrary : public LibraryInterface {
public:
    void Init() override;
    void Shutdown() override;
};

class LibraryInitializer {
public:
    LibraryInitializer();
    // A no-op method to force the linker to reference this class
    int summon();
};

} // namespace internal
} // namespace connx

#endif // CONNX_SRC_CORE_LIBRARY_H_
