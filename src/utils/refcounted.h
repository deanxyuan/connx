/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_UTILS_REFCOUNTED_H_
#define CONNX_SRC_UTILS_REFCOUNTED_H_

#include <atomic>

namespace connx {

// PolymorphicRefCount enforces polymorphic destruction of RefCounted.
class PolymorphicRefCount {
public:
    virtual ~PolymorphicRefCount() {}
};

// NonPolymorphicRefCount does not enforce polymorphic destruction of
// RefCounted. When in doubt use PolymorphicRefCount.
class NonPolymorphicRefCount {
public:
    ~NonPolymorphicRefCount() {}
};

template <typename Child, typename Impl = PolymorphicRefCount>
class RefCounted : public Impl {
public:
    explicit RefCounted()
        : refs_(1) {}
    ~RefCounted() = default;

    RefCounted(const RefCounted&) = delete;
    RefCounted& operator=(const RefCounted&) = delete;

    void Ref() { refs_.fetch_add(1, std::memory_order_relaxed); }

    void Unref() {
        if (refs_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete static_cast<Child*>(this);
        }
    }

private:
    std::atomic<intptr_t> refs_;
};

} // namespace connx
#endif // CONNX_SRC_UTILS_REFCOUNTED_H_
