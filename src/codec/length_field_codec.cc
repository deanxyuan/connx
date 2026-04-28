/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */
#include "connx/codec/length_field_codec.h"

#include "src/utils/useful.h"

namespace connx {

LengthFieldCodec::LengthFieldCodec(uint32_t length_field_offset, uint32_t length_field_length,
                                   uint32_t header_len, bool network_to_host)
    : length_field_offset_(length_field_offset)
    , length_field_length_(length_field_length)
    , header_len_(header_len)
    , network_to_host_(network_to_host) {
    // Validate constructor arguments. CONNX_ASSERT stays active in all build
    // configurations (unlike standard assert, which is removed under NDEBUG).
    CONNX_ASSERT(length_field_offset < header_len);
    CONNX_ASSERT((length_field_length_ >= 1 && length_field_length_ <= 8));
    CONNX_ASSERT(length_field_length_ <= header_len_);
}
LengthFieldCodec::~LengthFieldCodec() {}
DecodeResult LengthFieldCodec::Decode(const char* data, size_t len, size_t* consumed_len) {
    // Initialize output parameters.
    *consumed_len = 0;

    // Step 1: Check if we have enough data to read the entire header.
    if (len < header_len_) {
        return DecodeResult::kNeedMoreData;
    }

    // Step 2: Parse the length field (first 'length_field_length_' bytes of the header).
    uint64_t body_length = ReadLengthValue(data + length_field_offset_);

    // Step 3: Validate body length for sanity and overflow safety.
    // - body_length >= INT64_MAX: platform-independent sanity bound; reject unreasonably
    //   large frame declarations regardless of size_t width.
    // - body_length > SIZE_MAX - header_len_: guards against overflow when casting to
    //   size_t on platforms where SIZE_MAX < INT64_MAX (e.g. 32-bit).
    if (body_length >= INT64_MAX || body_length > SIZE_MAX - header_len_) {
        return DecodeResult::kError;
    }

    // Step 4: Calculate the total expected frame size.
    *consumed_len = header_len_ + static_cast<size_t>(body_length);

    return (len < *consumed_len) ? DecodeResult::kNeedMoreData : DecodeResult::kSuccess;
}

uint64_t LengthFieldCodec::ReadLengthValue(const char* data) {
    uint64_t value = 0;
    if (network_to_host_) {
        // Big-endian (network order): MSB first
        for (uint32_t i = 0; i < length_field_length_; i++) {
            value = (value << 8) | static_cast<uint8_t>(data[i]);
        }
    } else {
        // Little-endian (host order): LSB first
        for (uint32_t i = 0; i < length_field_length_; i++) {
            value |= static_cast<uint64_t>(static_cast<uint8_t>(data[i])) << (i * 8);
        }
    }
    return value;
}
} // namespace connx
