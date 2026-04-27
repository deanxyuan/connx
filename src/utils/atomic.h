/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_UTILS_ATOMIC_H_
#define CONNX_SRC_UTILS_ATOMIC_H_

#include <stdint.h>
#include <atomic>

namespace connx {
enum class MemoryOrder {
    RELAXED = std::memory_order_relaxed,
    CONSUME = std::memory_order_consume,
    ACQUIRE = std::memory_order_acquire,
    RELEASE = std::memory_order_release,
    ACQ_REL = std::memory_order_acq_rel,
    SEQ_CST = std::memory_order_seq_cst
};

template <typename T>
class Atomic {
public:
    explicit Atomic(T val = T())
        : value_(val) {}

    T Load(MemoryOrder order = MemoryOrder::RELAXED) const {
        return value_.load(static_cast<std::memory_order>(order));
    }

    void Store(T val, MemoryOrder order = MemoryOrder::RELAXED) {
        value_.store(val, static_cast<std::memory_order>(order));
    }

    T Exchange(T desired, MemoryOrder order) {
        return value_.exchange(desired, static_cast<std::memory_order>(order));
    }

    bool CompareExchangeWeak(T* expected, T desired, MemoryOrder success, MemoryOrder failure) {
        return value_.compare_exchange_weak(*expected, desired,
                                            static_cast<std::memory_order>(success),
                                            static_cast<std::memory_order>(failure));
    }

    bool CompareExchangeStrong(T* expected, T desired, MemoryOrder success, MemoryOrder failure) {
        return value_.compare_exchange_strong(*expected, desired,
                                              static_cast<std::memory_order>(success),
                                              static_cast<std::memory_order>(failure));
    }

    template <typename Arg>
    T FetchAdd(Arg arg, MemoryOrder order = MemoryOrder::SEQ_CST) {
        return value_.fetch_add(static_cast<Arg>(arg), static_cast<std::memory_order>(order));
    }

    template <typename Arg>
    T FetchSub(Arg arg, MemoryOrder order = MemoryOrder::SEQ_CST) {
        return value_.fetch_sub(static_cast<Arg>(arg), static_cast<std::memory_order>(order));
    }

    bool IncrementIfNonzero(MemoryOrder load_order = MemoryOrder::ACQUIRE) {
        T count = value_.load(static_cast<std::memory_order>(load_order));
        do {
            if (count == 0) {
                return false;
            }
        } while (!CompareExchangeWeak(&count, count + 1, MemoryOrder::ACQ_REL, load_order));
        return true;
    }

private:
    std::atomic<T> value_;
};

using AtomicBool = Atomic<bool>;
using AtomicInt32 = Atomic<int32_t>;
using AtomicInt64 = Atomic<int64_t>;
using AtomicUInt32 = Atomic<uint32_t>;
using AtomicUInt64 = Atomic<uint64_t>;
using AtomicIntptr = Atomic<intptr_t>;

} // namespace connx

#endif // CONNX_SRC_UTILS_ATOMIC_H_
