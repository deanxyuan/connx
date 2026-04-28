/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_INCLUDE_CODEC_LENGTH_FIELD_CODEC_H_
#define CONNX_INCLUDE_CODEC_LENGTH_FIELD_CODEC_H_

#include <cstddef>
#include <cstdint>
#include "connx/codec.h"

namespace connx {

/**
 * @brief Codec for protocols where length is specified in the header
 * +------------------+----------------------+------------------+
 * | Length Field (N) | Optional Header (H)  | Body (L bytes)   |
 * +------------------+----------------------+------------------+
 *
 * All size parameters use uint32_t to provide a consistent ABI across
 * platforms and implicitly bound the maximum frame size (~4 GB).
 */
class CONNX_API LengthFieldCodec : public Codec {
public:
    // @param length_field_offset offset of the length field within header (bytes)
    // @param length_field_length Size of the length field (1, 2, 4, or 8 bytes)
    // @param header_len Total length of the header including length field (bytes)
    // @param network_to_host true for network byte order (big-endian), false for host order
    LengthFieldCodec(uint32_t length_field_offset, uint32_t length_field_length = 4,
                     uint32_t header_len = 4, bool network_to_host = true);
    ~LengthFieldCodec();

    DecodeResult Decode(const char* data, size_t len, size_t* consumed_len) override;

private:
    uint64_t ReadLengthValue(const char* ptr);
    uint32_t length_field_offset_;
    uint32_t length_field_length_;
    uint32_t header_len_;
    bool network_to_host_;
};

} // namespace connx
#endif // CONNX_INCLUDE_CODEC_LENGTH_FIELD_CODEC_H_
