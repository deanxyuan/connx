/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_UTILS_STRING_H_
#define CONNX_SRC_UTILS_STRING_H_

#include <stddef.h>
#include <string.h>
#include <string>

void* connx_malloc(size_t size);
char* connx_strdup(const char* src);
char* connx_strdup(const char* src, size_t size);
int connx_format(char** strp, const char* format, ...);

#ifdef _WIN32
char* win32_error_message(int messageid);
#endif
char* posix_error_message(int err);

namespace connx {
template <typename... ARGS>
inline char* AsFormat(const char* format, ARGS... args) {
    char* strp = nullptr;
    connx_format(&strp, format, std::forward<ARGS>(args)...);
    return strp;
}
std::string FormatErrorMessage(int err);
} // namespace connx
#endif // CONNX_SRC_UTILS_STRING_H_
