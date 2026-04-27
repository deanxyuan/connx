/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_INCLUDE_CODEC_FIXED_LENGTH_CODEC_H_
#define CONNX_INCLUDE_CODEC_FIXED_LENGTH_CODEC_H_
#include "connx/codec.h"

namespace connx {

// Codec for protocols with a fixed frame length
class CONNX_API FixedLengthCodec : public Codec {
public:
    explicit FixedLengthCodec(size_t fixed_length);
    ~FixedLengthCodec();

    DecodeResult Decode(const char* data, size_t len, size_t* consumed_len) override;

private:
    size_t fixed_length_;
};

} // namespace connx
#endif // CONNX_INCLUDE_CODEC_FIXED_LENGTH_CODEC_H_
