/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_UTILS_TIME_H_
#define CONNX_SRC_UTILS_TIME_H_

#include <stdint.h>

typedef enum {
    CONNX_CLOCK_REALTIME = 0,
    CONNX_CLOCK_MONOTONIC,
} connx_clock_t;

struct connx_timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};
void connx_time_init();
connx_timespec connx_now(connx_clock_t type);
namespace connx {
int64_t GetCurrentMillisec();
}
#endif // CONNX_SRC_UTILS_TIME_H_
