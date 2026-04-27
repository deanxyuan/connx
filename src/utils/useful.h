/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_UTILS_USEFUL_H_
#define CONNX_SRC_UTILS_USEFUL_H_

#include <stdlib.h>

#include "src/utils/log.h"

#ifdef __GNUC__
#    define CONNX_LIKELY(x)   __builtin_expect((x), 1)
#    define CONNX_UNLIKELY(x) __builtin_expect((x), 0)
#else
#    define CONNX_LIKELY(x)   (x)
#    define CONNX_UNLIKELY(x) (x)
#endif

#define CONNX_MIN(a, b) ((a) < (b) ? (a) : (b))
#define CONNX_MAX(a, b) ((a) > (b) ? (a) : (b))

#define CONNX_ARRAY_SIZE(array) (sizeof(array) / sizeof(*(array)))

/** Set the \a n-th bit of \a i (a mutable pointer). */
#define CONNX_BIT_SET(i, n) ((*(i)) |= (1u << (n)))

/** Clear the \a n-th bit of \a i (a mutable pointer). */
#define CONNX_BIT_CLEAR(i, n) ((*(i)) &= ~(1u << (n)))

/** Get the \a n-th bit of \a i */
#define CONNX_BIT_GET(i, n) (((i) & (1u << (n))) != 0)

#define CONNX_ASSERT(x)                                                                            \
    do {                                                                                           \
        if (CONNX_UNLIKELY(!(x))) {                                                                \
            connx_log(CONNX_LOG_LEVEL_ERROR, __LINE__, "assertion failed: %s", #x);                \
            abort();                                                                               \
        }                                                                                          \
    } while (false)

#endif // CONNX_SRC_UTILS_USEFUL_H_
