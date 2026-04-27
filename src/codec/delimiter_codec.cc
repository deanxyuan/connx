/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "connx/codec/delimiter_codec.h"
#include <string.h>

namespace connx {

DelimiterCodec::DelimiterCodec(char delimiter)
    : delimiter_(delimiter) {}

DecodeResult DelimiterCodec::Decode(const char* data, size_t len, size_t* consumed_len) {
    *consumed_len = 0;

    const char* pos = static_cast<const char*>(memchr(data, delimiter_, len));

    if (pos != nullptr) {
        *consumed_len = (pos - data) + 1; // + delimiter
        return DecodeResult::kSuccess;
    }
    return DecodeResult::kNeedMoreData;
}

} // namespace connx
