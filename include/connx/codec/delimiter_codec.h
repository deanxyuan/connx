/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_INCLUDE_CODEC_DELIMITER_CODEC_H_
#define CONNX_INCLUDE_CODEC_DELIMITER_CODEC_H_
#include "connx/codec.h"

namespace connx {

// Codec for protocols ending with a specific delimiter (e.g., '\n')
class CONNX_API DelimiterCodec : public Codec {
public:
    explicit DelimiterCodec(char delimiter);
    ~DelimiterCodec() override = default;

    DecodeResult Decode(const char* data, size_t len, size_t* consumed_len) override;

private:
    char delimiter_;
};

} // namespace connx
#endif // CONNX_INCLUDE_CODEC_DELIMITER_CODEC_H_
