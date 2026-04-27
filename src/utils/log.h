/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_UTILS_LOG_H_
#define CONNX_SRC_UTILS_LOG_H_

#include "connx/c.h"

extern "C" void connx_log(int level, int line, const char* format, ...);

#define CONNX_LOG_TRACE(fmt, ...)                                                                  \
    do {                                                                                           \
        if (connx_log_get_min_level() <= CONNX_LOG_LEVEL_TRACE) {                                  \
            connx_log(CONNX_LOG_LEVEL_TRACE, __LINE__, fmt, ##__VA_ARGS__);                        \
        }                                                                                          \
    } while (0)

#define CONNX_LOG_DEBUG(fmt, ...)                                                                  \
    do {                                                                                           \
        if (connx_log_get_min_level() <= CONNX_LOG_LEVEL_DEBUG) {                                  \
            connx_log(CONNX_LOG_LEVEL_DEBUG, __LINE__, fmt, ##__VA_ARGS__);                        \
        }                                                                                          \
    } while (0)

#define CONNX_LOG_INFO(fmt, ...)                                                                   \
    do {                                                                                           \
        if (connx_log_get_min_level() <= CONNX_LOG_LEVEL_INFO) {                                   \
            connx_log(CONNX_LOG_LEVEL_INFO, __LINE__, fmt, ##__VA_ARGS__);                         \
        }                                                                                          \
    } while (0)

#define CONNX_LOG_WARN(fmt, ...)                                                                   \
    do {                                                                                           \
        if (connx_log_get_min_level() <= CONNX_LOG_LEVEL_WARN) {                                   \
            connx_log(CONNX_LOG_LEVEL_WARN, __LINE__, fmt, ##__VA_ARGS__);                         \
        }                                                                                          \
    } while (0)

#define CONNX_LOG_ERROR(fmt, ...)                                                                  \
    do {                                                                                           \
        if (connx_log_get_min_level() <= CONNX_LOG_LEVEL_ERROR) {                                  \
            connx_log(CONNX_LOG_LEVEL_ERROR, __LINE__, fmt, ##__VA_ARGS__);                        \
        }                                                                                          \
    } while (0)

#endif // CONNX_SRC_UTILS_LOG_H_
