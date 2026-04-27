/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "src/utils/string.h"
#ifdef _WIN32
#    include <Windows.h>
#    include <UserEnv.h>
#else
#    include <unistd.h>
#    include <fcntl.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

void* connx_malloc(size_t size) {
    void* p = malloc(size);
    if (!p) {
        abort();
    }
    return p;
}

char* connx_strdup(const char* src) { return connx_strdup(src, strlen(src)); }

char* connx_strdup(const char* src, size_t size) {
    size_t len = size + 1;
    char* dst = static_cast<char*>(connx_malloc(len));
    memcpy(dst, src, size);
    dst[size] = '\0';
    return dst;
}

#ifdef _WIN32
int connx_format(char** strp, const char* format, ...) {
    va_list args;
    int ret;
    size_t strp_buflen;

    va_start(args, format);
    ret = _vscprintf(format, args);
    va_end(args);

    if (ret < 0) {
        *strp = nullptr;
        return -1;
    }

    strp_buflen = static_cast<size_t>(ret) + 1;
    if ((*strp = static_cast<char*>(malloc(strp_buflen))) == NULL) {
        return -1;
    }

    // try again using the larger buffer.
    va_start(args, format);
    ret = vsnprintf_s(*strp, strp_buflen, _TRUNCATE, format, args);
    va_end(args);

    // check return
    if (static_cast<size_t>(ret) == strp_buflen - 1) {
        return ret;
    }

    // this should never happen.
    free(*strp);
    *strp = NULL;
    return -1;
}

#else
int connx_format(char** strp, const char* format, ...) {
    va_list args;
    int ret;
    char buf[128];
    size_t strp_buflen;

    va_start(args, format);
    ret = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    if (ret < 0) {
        *strp = NULL;
        return -1;
    }

    strp_buflen = static_cast<size_t>(ret) + 1;
    if ((*strp = static_cast<char*>(malloc(strp_buflen))) == NULL) {
        return -1;
    }

    if (strp_buflen <= sizeof(buf)) {
        memcpy(*strp, buf, strp_buflen);
        return ret;
    }

    // try again using the larger buffer.
    va_start(args, format);
    ret = vsnprintf(*strp, strp_buflen, format, args);
    va_end(args);
    if (static_cast<size_t>(ret) == strp_buflen - 1) {
        return ret;
    }

    // this should never happen.
    free(*strp);
    *strp = NULL;
    return -1;
}
#endif

#ifdef _WIN32
char* win32_error_message(int messageid) {
    char* error_text = NULL;
    // Use MBCS version of FormatMessage to match return value.
    DWORD bytes = ::FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, (DWORD)messageid, MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT),
        reinterpret_cast<char*>(&error_text), 0, NULL);
    if (bytes == 0) return connx_strdup("Unable to retrieve error string");
    while (bytes > 0 && (error_text[bytes - 1] == '\r' || error_text[bytes - 1] == '\n')) {
        bytes--;
    }
    char* message = connx_strdup(error_text, bytes);
    ::LocalFree(error_text);
    return message;
}
#endif

char* posix_error_message(int err) {
    char buf[512] = {0};
#ifdef _WIN32
    strerror_s(buf, sizeof(buf), err);
#else
    strerror_r(err, buf, sizeof(buf));
#endif
    return connx_strdup(buf);
}

namespace connx {
std::string FormatErrorMessage(int err) {
#ifdef _WIN32
    char* desc = win32_error_message(err);
#else
    char desc[512] = {0};
    strerror_r(err, desc, sizeof(desc));
#endif
    char* message = nullptr;
    int n = connx_format(&message, "code: %d (%s)", err, desc);
    std::string text;
    if (message) {
        text = (n > 0) ? std::string(message, n) : std::string();
        free(message);
    }
#ifdef _WIN32
    free(desc);
#endif
    return text;
}
} // namespace connx
