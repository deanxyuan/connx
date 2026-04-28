/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "test/util/testutil.h"
#include "src/utils/slice.h"

#include <string.h>

RUN_ALL_TESTS();

using namespace connx;

// Construction

TEST(SliceTest, default_constructor) {
    Slice s;
    ASSERT_EQ(s.size(), (size_t)0);
    ASSERT_TRUE(s.empty());
}
TEST(SliceTest, from_cstring) {
    Slice s("hello");
    ASSERT_EQ(s.size(), (size_t)5);
    ASSERT_TRUE(s == "hello");
}
TEST(SliceTest, from_buffer_and_length) {
    const char* data = "abcdef";
    Slice s(data, 3);
    ASSERT_EQ(s.size(), (size_t)3);
    ASSERT_TRUE(s == "abc");
}

TEST(SliceTest, from_empty_buffer) {
    Slice s(static_cast<const char*>(nullptr), (size_t)0);
    ASSERT_EQ(s.size(), (size_t)0);
    ASSERT_TRUE(s.empty());
}

// Copy and Move

TEST(SliceTest, copy) {
    Slice a("hello");
    Slice b(a);
    ASSERT_EQ(b.size(), (size_t)5);
    ASSERT_TRUE(b == "hello");
}
TEST(SliceTest, copy_assignment) {
    Slice a("hello");
    Slice b;
    b = a;
    ASSERT_EQ(b.size(), (size_t)5);
    ASSERT_TRUE(b == "hello");
}
TEST(SliceTest, move) {
    Slice a("hello");
    Slice b(std::move(a));
    ASSERT_EQ(b.size(), (size_t)5);
    ASSERT_TRUE(b == "hello");
}
TEST(SliceTest, move_assignment) {
    Slice a("hello");
    Slice b;
    b = std::move(a);
    ASSERT_EQ(b.size(), (size_t)5);
    ASSERT_TRUE(b == "hello");
}

// Comparison

TEST(SliceTest, equality) {
    Slice a("abc");
    Slice b("abc");
    ASSERT_TRUE(a == b);
}
TEST(SliceTest, inequality) {
    Slice a("abc");
    Slice b("def");
    ASSERT_TRUE(a != b);
}
TEST(SliceTest, compare_different_length) {
    Slice a("abc");
    Slice b("abcd");
    ASSERT_TRUE(a != b);
}

// Mutation

TEST(SliceTest, remove_head) {
    Slice s("hello world");
    s.RemoveHead(6);
    ASSERT_EQ(s.size(), (size_t)5);
    ASSERT_TRUE(s == "world");
}
TEST(SliceTest, remove_tail) {
    Slice s("hello world");
    s.RemoveTail(6);
    ASSERT_EQ(s.size(), (size_t)5);
    ASSERT_TRUE(s == "hello");
}
TEST(SliceTest, clear) {
    Slice s("hello");
    s.clear();
    ASSERT_EQ(s.size(), (size_t)0);
    ASSERT_TRUE(s.empty());
}

// Concatenation

TEST(SliceTest, operator_plus) {
    Slice a("hello ");
    Slice b("world");
    Slice c = a + b;
    ASSERT_EQ(c.size(), (size_t)11);
    ASSERT_TRUE(c == "hello world");
}
TEST(SliceTest, operator_pluseq) {
    Slice a("hello ");
    Slice b("world");
    a += b;
    ASSERT_EQ(a.size(), (size_t)11);
    ASSERT_TRUE(a == "hello world");
}

// Factory Functions

TEST(SliceTest, make_slice_by_length) {
    Slice s = MakeSliceByLength(100);
    ASSERT_EQ(s.size(), (size_t)100);
}
TEST(SliceTest, make_slice_default) {
    Slice s = MakeSliceDefault();
    ASSERT_TRUE(s.size() > 0);
}

// Conversion
TEST(SliceTest, to string) {
    Slice s("hello");
    std::string str = SliceToString(s);
    ASSERT_EQ(str, std::string("hello"));
}
TEST(SliceTest, from_string) {
    std::string str("hello");
    Slice s = SliceFromString(str);
    ASSERT_EQ(s.size(), (size_t)5);
    ASSERT_TRUE(s == "hello");
}
TEST(SliceTest, string_comparison) {
    Slice s("hello");
    ASSERT_TRUE(s == std::string("hello"));
    ASSERT_TRUE(std::string("hello") == s);
    ASSERT_TRUE(s == "hello");
    ASSERT_TRUE("hello" == s);
}
// Accessors

TEST(SliceTest, begin_end) {
    Slice s("abc");
    ASSERT_EQ(s.end() - s.begin(), (ptrdiff_t)3);
}
TEST(SliceTest, data_buffer) {
    Slice s("abc");
    ASSERT_EQ(s.data(), s.begin());
    ASSERT_EQ(memcmp(s.buffer(), "abc", 3), 0);
}
