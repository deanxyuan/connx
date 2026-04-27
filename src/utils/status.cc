/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "src/utils/status.h"
#include "src/utils/string.h"

namespace connx {
Status::Status()
    : err_code_(0) {}

Status::~Status() {}

Status::Status(const std::string& msg)
    : Status(-1, msg) {}

Status::Status(int err_code, const std::string& err_msg)
    : err_code_(err_code)
    , desc_(err_msg) {}

Status::Status(const Status& oth)
    : err_code_(oth.err_code_)
    , desc_(oth.desc_) {}

Status& Status::operator=(const Status& oth) {
    if (this != &oth) {
        err_code_ = oth.err_code_;
        desc_ = oth.desc_;
    }
    return *this;
}

Status::Status(Status&& other) noexcept
    : err_code_(other.err_code_) {
    desc_ = std::move(other.desc_);
    other.err_code_ = 0;
}

Status& Status::operator=(Status&& other) noexcept {
    err_code_ = other.err_code_;
    desc_ = std::move(other.desc_);
    other.err_code_ = 0;
    return *this;
}

int Status::ErrorCode() const { return err_code_; }

std::string Status::ToString() const {
    if (err_code_ == 0) {
        return std::string("No error");
    }

    int n = 0;
    char* text = nullptr;
    if (desc_.empty()) {
        n = connx_format(&text, "code <%d>.", err_code_);
    } else {
        n = connx_format(&text, "code <%d>, %s.", err_code_, desc_.c_str());
    }
    std::string rsp;
    if (text) {
        rsp = (n > 0) ? std::string(text, n) : std::string();
        free(text);
    }
    return rsp;
}

std::shared_ptr<Status> MakeStatusFromPosixError(int err, const char* api) {
    char* message = nullptr;
#ifdef _WIN32
    int n = connx_format(&message, "'%s'-[%s]", api, strerror(err));
#else
    char desc[256] = {0};
    strerror_r(err, desc, sizeof(desc));
    int n = connx_format(&message, "'%s'-[%s]", api, desc);
#endif
    if (!message) return nullptr;
    std::string str = (n > 0) ? std::string(message, n) : std::string();
    std::shared_ptr<Status> obj = std::make_shared<Status>(err, str);
    free(message);
    return obj;
}

#ifdef _WIN32
std::shared_ptr<Status> MakeStatusFromWin32Error(int err, const char* api) {
    char* desc = win32_error_message(err);
    char* message = nullptr;
    int n = connx_format(&message, "'%s'-[%s]", api, desc);
    if (!message) {
        free(desc);
        return nullptr;
    }
    std::string str = (n > 0) ? std::string(message, n) : std::string();
    std::shared_ptr<Status> obj = std::make_shared<Status>(err, str);
    free(message);
    free(desc);
    return obj;
}
#endif

} // namespace connx
