/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_UTILS_STATUS_H_
#define CONNX_SRC_UTILS_STATUS_H_

#include <stdlib.h>
#include <string>
#include <memory>
#include <errno.h>

#include "src/utils/string.h"

namespace connx {

class Status final {
public:
    Status();
    ~Status();

    Status(const std::string& msg);
    Status(int err_code, const std::string& err_msg);

    Status(const Status& oth);
    Status& operator=(const Status& oth);
    Status(Status&& other) noexcept;
    Status& operator=(Status&& other) noexcept;

    int ErrorCode() const;
    std::string ToString() const;

private:
    // 0: no error
    int err_code_;

    // description
    std::string desc_;
};

static inline std::shared_ptr<Status> MakeStatusFromStaticString(const char* msg) {
    return std::make_shared<Status>(msg);
}

#ifdef _WIN32
std::shared_ptr<Status> MakeStatusFromWin32Error(int err, const char* api);
#endif

template <typename... Args>
inline std::shared_ptr<Status> MakeStatusFromFormat(Args&&... args) {
    char* message = nullptr;
    int n = connx_format(&message, std::forward<Args>(args)...);
    if (!message) return nullptr;
    std::string str = (n > 0) ? std::string(message, n) : std::string();
    auto obj = std::make_shared<Status>(str);
    free(message);
    return obj;
}
std::shared_ptr<Status> MakeStatusFromPosixError(int err, const char* api);

} // namespace connx

using connx_error = std::shared_ptr<connx::Status>;
#define CONNX_ERROR_NONE nullptr

#define CONNX_POSIX_ERROR(err, api_name) connx::MakeStatusFromPosixError(err, api_name)
#ifdef _WIN32
#    define CONNX_SYSTEM_ERROR(err, api_name) connx::MakeStatusFromWin32Error(err, api_name)
#else
#    define CONNX_SYSTEM_ERROR(err, api_name) connx::MakeStatusFromPosixError(err, api_name)
#endif
#define CONNX_ERROR_FROM_STATIC_STRING(message) connx::MakeStatusFromStaticString(message)
#define CONNX_ERROR_FROM_FORMAT(FMT, ...)       connx::MakeStatusFromFormat(FMT, ##__VA_ARGS__)

#endif // CONNX_SRC_UTILS_STATUS_H_
