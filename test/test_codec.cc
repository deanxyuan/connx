/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "test/testutil.h"
#include "connx/codec/delimiter_codec.h"
#include "connx/codec/fixed_length_codec.h"
#include "connx/codec/length_field_codec.h"
#include "src/utils/endian.h"

#include <string.h>

RUN_ALL_TESTS();

using namespace connx;

// DelimiterCodec Tests
TEST(DelimiterCodecTest, basic_newline) {
    DelimiterCodec codec('\n');
    const char* data = "hello\nworld";
    size_t consumed = 0;
    auto result = codec.Decode(data, strlen(data), &consumed);
    ASSERT_TRUE(result == DecodeResult::kSuccess);
    ASSERT_EQ(consumed, (size_t)6); // "hello(n"
}
TEST(DelimiterCodecTest, delimiter_at_end) {
    DelimiterCodec codec('\n');
    const char* data = "hello\n";
    size_t consumed = 0;
    auto result = codec.Decode(data, strlen(data), &consumed);
    ASSERT_TRUE(result == DecodeResult::kSuccess);
    ASSERT_EQ(consumed, (size_t)6);
}
TEST(DelimiterCodecTest, need_more_data) {
    DelimiterCodec codec('\n');
    const char* data = "hello";
    size_t consumed = 99;
    auto result = codec.Decode(data, strlen(data), &consumed);
    ASSERT_TRUE(result == DecodeResult::kNeedMoreData);
    ASSERT_EQ(consumed, (size_t)0);
}

TEST(DelimiterCodecTest, empty_input) {
    DelimiterCodec codec('\n');
    size_t consumed = 0;
    auto result = codec.Decode("", 0, &consumed);
    ASSERT_TRUE(result == DecodeResult::kNeedMoreData);
    ASSERT_EQ(consumed, (size_t)0);
}

TEST(DelimiterCodecTest, first_byte_is_delimiter) {
    DelimiterCodec codec('\n');
    const char* data = "\nhello";
    size_t consumed = 0;
    auto result = codec.Decode(data, strlen(data), &consumed);
    ASSERT_TRUE(result == DecodeResult::kSuccess);
    ASSERT_EQ(consumed, (size_t)1);
}
TEST(DelimiterCodecTest, custom_delimiter) {
    DelimiterCodec codec('|');
    const char* data = "abc|def";
    size_t consumed = 0;
    auto result = codec.Decode(data, strlen(data), &consumed);
    ASSERT_TRUE(result == DecodeResult::kSuccess);
    ASSERT_EQ(consumed, (size_t)4); //"abc|"
}

TEST(DelimiterCodecTest, multiple_delimiters) {
    DelimiterCodec codec('\n');
    const char* data = "line1\nline2\nline3\n";
    size_t consumed = 0;
    auto r1 = codec.Decode(data, strlen(data), &consumed);
    ASSERT_TRUE(r1 == DecodeResult::kSuccess);
    ASSERT_EQ(consumed, (size_t)6); // "line1\n"
    auto r2 = codec.Decode(data + consumed, strlen(data) - consumed, &consumed);
    ASSERT_TRUE(r2 == DecodeResult::kSuccess);
    ASSERT_EQ(consumed, (size_t)6); // "line2(n"
    auto r3 = codec.Decode(data + 12, strlen(data) - 12, &consumed);
    ASSERT_TRUE(r3 == DecodeResult::kSuccess);
    ASSERT_EQ(consumed, (size_t)6); // "line3(n"
}

// FixedLengthCodec Tests

TEST(FixedLengthCodecTest, exact_length) {
    FixedLengthCodec codec(5);
    const char* data = "hello";
    size_t consumed = 0;
    auto result = codec.Decode(data, 5, &consumed);
    ASSERT_TRUE(result == DecodeResult::kSuccess);
    ASSERT_EQ(consumed, (size_t)5);
}
TEST(FixedLengthCodecTest, more_than_length) {
    FixedLengthCodec codec(3);
    const char* data = "abcdef";
    size_t consumed = 0;
    auto result = codec.Decode(data, 6, &consumed);
    ASSERT_TRUE(result == DecodeResult::kSuccess);
    ASSERT_EQ(consumed, (size_t)3);
}
TEST(FixedLengthCodecTest, less_than_length) {
    FixedLengthCodec codec(10);
    const char* data = "abc";
    size_t consumed = 99;
    auto result = codec.Decode(data, 3, &consumed);
    ASSERT_TRUE(result == DecodeResult::kNeedMoreData);
    ASSERT_EQ(consumed, (size_t)10);
}
TEST(FixedLengthCodecTest, zero_input) {
    FixedLengthCodec codec(5);
    size_t consumed = 0;
    auto result = codec.Decode("", 0, &consumed);
    ASSERT_TRUE(result == DecodeResult::kNeedMoreData);
    ASSERT_EQ(consumed, (size_t)5);
}

TEST(FixedLengthCodecTest, zero_length_clamps_to_one) {
    FixedLengthCodec codec(0);
    const char* data = "a";
    size_t consumed = 0;
    auto result = codec.Decode(data, 1, &consumed);
    ASSERT_TRUE(result == DecodeResult::kSuccess);
    ASSERT_EQ(consumed, (size_t)1);
}

// LengthFieldCodec Tests
TEST(LengthFieldCodecTest, basic_big_endian) {
    // Header: 4 bytes (length field is the entire header)
    // Length field:offset=0,length=4,header_len=4,network_to_host=true
    LengthFieldCodec codec(0, 4, 4, true);
    // Build frame:header(4 bytes big-endian length=5) + body("hello")
    uint32_t body_len = detail::host_to_network<uint32_t>(5);
    char frame[9];
    memcpy(frame, &body_len, 4);
    memcpy(frame + 4, "hello", 5);
    size_t consumed = 0;
    auto result = codec.Decode(frame, sizeof(frame), &consumed);
    ASSERT_TRUE(result == DecodeResult::kSuccess);
    ASSERT_EQ(consumed, (size_t)9); // 4header + 5 body
}
TEST(LengthFieldCodecTest, need_more_header) {
    LengthFieldCodec codec(0, 4, 4, true);
    char partial[] = {0x00, 0x00}; // only 2 bytes,need 4 for header
    size_t consumed = 0;
    auto result = codec.Decode(partial, 2, &consumed);
    ASSERT_TRUE(result == DecodeResult::kNeedMoreData);
    ASSERT_EQ(consumed, (size_t)0);
}
TEST(LengthFieldCodecTest, need_more_body) {
    LengthFieldCodec codec(0, 4, 4, true);
    // Header says body is 10 bytes,but we only provide 2
    uint32_t body_len = detail::host_to_network<uint32_t>(10);
    char frame[6];
    memcpy(frame, &body_len, 4);
    memcpy(frame + 4, "ab", 2);
    size_t consumed = 0;
    auto result = codec.Decode(frame, sizeof(frame), &consumed);
    ASSERT_TRUE(result == DecodeResult::kNeedMoreData);
    ASSERT_EQ(consumed, (size_t)14); // expectedtotal: 4 + 10
}
TEST(LengthFieldCodecTest, two_byte_length_field) {
    // header_len=4,lengthfield at offset=0,2 bytes long
    LengthFieldCodec codec(0, 2, 4, true);
    // Frame: 2-byte length(big-endian,value=3) + 2-byte extra header + 3-byte body
    uint16_t body_len = detail::host_to_network<uint16_t>(3);
    char frame[7];
    memcpy(frame, &body_len, 2);
    frame[2] = (char)0xAA; // extra header byte
    frame[3] = (char)0xBB; // extra header byte
    memcpy(frame + 4, "abc", 3);
    size_t consumed = 0;
    auto result = codec.Decode(frame, sizeof(frame), &consumed);
    ASSERT_TRUE(result == DecodeResult::kSuccess);
    ASSERT_EQ(consumed, (size_t)7); // 4 header + 3 body
}
TEST(LengthFieldCodecTest, offset_length_field) {
    // header_len=6,length field at offset=2,2 bytes
    LengthFieldCodec codec(2, 2, 6, true);
    // Frame: 2 bytes prefix + 2 bytes length (big-endian,value=4) + 2 bytes suffix + 4 bytes body
    uint16_t body_len = detail::host_to_network<uint16_t>(4);
    char frame[10];
    frame[0] = 0x01; // prefix
    frame[1] = 0x02; // prefix
    memcpy(frame + 2, &body_len, 2);
    frame[4] = 0x03; // suffix
    frame[5] = 0x04; // suffix
    memcpy(frame + 6, "data", 4);
    size_t consumed = 0;
    auto result = codec.Decode(frame, sizeof(frame), &consumed);
    ASSERT_TRUE(result == DecodeResult::kSuccess);
    ASSERT_EQ(consumed, (size_t)10); // 6 header_+4body
}
TEST(LengthFieldCodecTest, little_endian_mode) {
    LengthFieldCodec codec(0, 4, 4, false);
    // Frame:4-byte length in little-endian (value=3)+ 3-byte body
    uint32_t body_len = 3; // alreadylittle-endian on x86
    char frame[7];
    memcpy(frame, &body_len, 4);
    memcpy(frame + 4, "abc", 3);
    size_t consumed = 0;
    auto result = codec.Decode(frame, sizeof(frame), &consumed);
    ASSERT_TRUE(result == DecodeResult::kSuccess);
    ASSERT_EQ(consumed, (size_t)7);
}
TEST(LengthFieldCodecTest, zero_body_length) {
    LengthFieldCodec codec(0, 4, 4, true);
    uint32_t body_len = detail::host_to_network<uint32_t>(0);
    char frame[4];
    memcpy(frame, &body_len, 4);
    size_t consumed = 0;
    auto result = codec.Decode(frame, sizeof(frame), &consumed);
    ASSERT_TRUE(result == DecodeResult::kSuccess);
    ASSERT_EQ(consumed, (size_t)4); // header only
}

// Endian utility tests

TEST(EndianTest, swap_bytes_u16) {
    ASSERT_EQ(connx_swap_bytes((uint16_t)0x0102), (uint16_t)0x0201);
}
TEST(EndianTest, swap_bytes_u32) {
    ASSERT_EQ(connx_swap_bytes((uint32_t)0x01020304), (uint32_t)0x04030201);
}
TEST(EndianTest, swap_bytes_u64) {
    ASSERT_EQ(connx_swap_bytes((uint64_t)0x0102030405060708), (uint64_t)0x0807060504030201);
}
TEST(EndianTest, host_network_roundtrip_u16) {
    uint16_t val = 0x1234;
    ASSERT_EQ(detail::network_to_host(detail::host_to_network(val)), val);
}
TEST(EndianTest, host_network_roundtrip_u32) {
    uint32_t val = 0x12345678;
    ASSERT_EQ(detail::network_to_host(detail::host_to_network(val)), val);
}
TEST(EndianTest, host_network_roundtrip_u64) {
    uint64_t val = 0x0123456789ABCDEFULL;
    ASSERT_EQ(detail::network_to_host(detail::host_to_network(val)), val);
}
