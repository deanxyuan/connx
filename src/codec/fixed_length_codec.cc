/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "connx/codec/fixed_length_codec.h"

namespace connx {

FixedLengthCodec::FixedLengthCodec(size_t fixed_length)
    : fixed_length_(fixed_length) {
    if (fixed_length_ == 0) {
        fixed_length_ = 1;
    }
}
FixedLengthCodec::~FixedLengthCodec() {}
DecodeResult FixedLengthCodec::Decode(const char* data, size_t len, size_t* consumed_len) {
    *consumed_len = fixed_length_;
    return (len < fixed_length_) ? DecodeResult::kNeedMoreData : DecodeResult::kSuccess;
}

} // namespace connx
