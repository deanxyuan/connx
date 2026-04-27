/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_UTILS_ENDIAN_H_
#define CONNX_SRC_UTILS_ENDIAN_H_

#include <cstdint>

// ============================================================================
// 1. Cross-Compiler Endianness Detection
// ============================================================================
// We define our own macro CONNX_IS_BIG_ENDIAN to abstract away differences
// between GCC, Clang, and MSVC. This avoids the need for system headers
// like <endian.h> (Linux) or <winsock2.h> (Windows).

#if defined(_MSC_VER)
#    include <intrin.h>
// Microsoft Visual C++ on x86/x64/ARM is always Little Endian.
#    define CONNX_IS_LITTLE_ENDIAN

#elif defined(__BYTE_ORDER__)
// Standard built-in macros from GCC and Clang.
#    if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#        define CONNX_IS_LITTLE_ENDIAN
#    elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#        define CONNX_IS_BIG_ENDIAN
#    else
#        error "connx: Unsupported byte order detected by compiler."
#    endif

#else
// Fallback for unknown compilers or very old toolchains.
// We cannot safely assume an endianness, so we force the user to define it.
#    error                                                                                         \
        "connx: Unable to determine endianness. Please define CONNX_IS_LITTLE_ENDIAN or CONNX_IS_BIG_ENDIAN."
#endif

// ============================================================================
// 2. Compiler Intrinsics (Byte Swapping)
// ============================================================================
// We use compiler built-in functions instead of system library functions.
// These are guaranteed to be inlined and optimized into single CPU instructions
// (e.g., BSWAP on x86) by the compiler.

static inline uint8_t connx_swap_bytes(uint8_t val) { return val; }
static inline uint16_t connx_swap_bytes(uint16_t val) {
    return static_cast<uint16_t>((val << 8) | (val >> 8));
}

static inline uint32_t connx_swap_bytes(uint32_t val) {
#if defined(_MSC_VER)
    return _byteswap_ulong(val); // MSVC Intrinsic
#else
    return __builtin_bswap32(val); // GCC/Clang Intrinsic
#endif
}

static inline uint64_t connx_swap_bytes(uint64_t val) {
#if defined(_MSC_VER)
    return _byteswap_uint64(val); // MSVC Intrinsic
#else
    return __builtin_bswap64(val); // GCC/Clang Intrinsic
#endif
}

// ============================================================================
// 3. Public API
// ============================================================================

namespace connx {
namespace detail {

// Converts a value from Host Byte Order to Network Byte Order (Big Endian).
// If the host is already Big Endian, this is a no-op.
template <typename T>
inline T host_to_network(T val) {
#ifdef CONNX_IS_BIG_ENDIAN
    return val;
#else
    return connx_swap_bytes(val);
#endif
}

// Converts a value from Network Byte Order (Big Endian) to Host Byte Order.
// Since swapping is symmetric, the logic is identical to host_to_network.
template <typename T>
inline T network_to_host(T val) {
    return host_to_network(val);
}

} // namespace detail
} // namespace connx
#endif // CONNX_SRC_UTILS_ENDIAN_H_
