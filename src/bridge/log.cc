/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "connx/c.h"
#include <time.h>

#ifdef _WIN32
#    include <Windows.h>
#    include <processthreadsapi.h>
#else
#    include <sys/syscall.h>
#    include <unistd.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <atomic>

#include "src/utils/useful.h"
#include "src/utils/time.h"
#include "src/utils/string.h"

extern "C" {
// 2023-04-12 16:16:00.000 thread_id line Info Message
#define CONNX_LOG_FORMAT "%s.%03ld %lu %d %s %s"

static const char* g_desc[] = {"Trace", "Debug", "Info", "Warn", "Error"};

static void connx_log_default(int level, int line, unsigned long threadid, const char* message,
                              void* user_data) {

    (void)user_data;
    char time_buffer[64] = {0};
    auto now = connx_now(CONNX_CLOCK_REALTIME);
    time_t timer = now.tv_sec;

#ifdef _WIN32
    struct tm stm;
    if (localtime_s(&stm, &timer)) {
        strcpy(time_buffer, "error:localtime");
    }
#else
    struct tm stm;
    if (!localtime_r(&timer, &stm)) {
        strcpy(time_buffer, "error:localtime");
    }
#endif
    // "%F %T" 2020-05-10 01:43:06
    else if (0 == strftime(time_buffer, sizeof(time_buffer), "%F %T", &stm)) {
        strcpy(time_buffer, "error:strftime");
    }

    char* output_text = NULL;
    connx_format(&output_text, CONNX_LOG_FORMAT, time_buffer,
                 now.tv_nsec / 1000000, // milliseconds
                 threadid, line, g_desc[static_cast<int>(level)], message);

    fprintf(stderr, "%s\n", output_text);
    free(output_text);
}

static std::atomic<intptr_t> g_log_function((intptr_t)connx_log_default);
static std::atomic<intptr_t> g_user_data((intptr_t)0);
static std::atomic<int> g_min_level(CONNX_LOG_LEVEL_DEBUG);

#ifndef _WIN32
static pid_t GetCurrentThreadId() { return syscall(SYS_gettid); }
#endif

void connx_log_set_min_level(int level) {
    if (level < CONNX_LOG_LEVEL_TRACE) {
        level = CONNX_LOG_LEVEL_TRACE;
    } else if (level > CONNX_LOG_LEVEL_ERROR) {
        level = CONNX_LOG_LEVEL_ERROR;
    }
    g_min_level = level;
}
int connx_log_get_min_level() { return g_min_level.load(std::memory_order_relaxed); }

void connx_log_set_callback(connx_log_callback_t callback, void* user_data) {
    g_log_function.store((intptr_t)callback, std::memory_order_relaxed);
    g_user_data.store((intptr_t)user_data, std::memory_order_relaxed);
}

void connx_log(int level, int line, const char* format, ...) {

    if (CONNX_UNLIKELY(level < CONNX_LOG_LEVEL_TRACE || level > CONNX_LOG_LEVEL_ERROR)) {
        return;
    }

    if (static_cast<int32_t>(level) < g_min_level.load(std::memory_order_relaxed)) {
        return;
    }

    char* message = NULL;
    va_list args;
    va_start(args, format);

#ifdef _WIN32
    int ret = _vscprintf(format, args);
    va_end(args);
    if (ret < 0) {
        return;
    }

    size_t buff_len = (size_t)ret + 1;
    message = (char*)malloc(buff_len);
    va_start(args, format);
    ret = vsnprintf_s(message, buff_len, _TRUNCATE, format, args);
    va_end(args);
#else
    if (vasprintf(&message, format, args) == -1) { // stdio.h
        va_end(args);
        return;
    }
#endif

    auto threadid = static_cast<unsigned long>(GetCurrentThreadId());
    void* usr_data = (void*)g_user_data.load(std::memory_order_relaxed);
    auto fn = (connx_log_callback_t)g_log_function.load(std::memory_order_relaxed);
    if (fn) {
        fn(level, line, threadid, message, usr_data);
    }
    free(message);
}
}
