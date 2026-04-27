/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "test/testutil.h"
#include "src/utils/slice_buffer.h"

#include <string.h>

RUN_ALL_TESTS();

using namespace connx;

// Basic operation

TEST(SliceBufferTest, empty) {
    SliceBuffer sb;
    ASSERT_TRUE(sb.Empty());
    ASSERT_EQ(sb.GetBufferLength(), (size_t)0);
    ASSERT_EQ(sb.SliceCount(), (size_t)0);
}
TEST(SliceBufferTest, add_lvalue) {
    SliceBuffer sb;
    Slice s("hello");
    sb.AddSlice(s);
    ASSERT_TRUE(!sb.Empty());
    ASSERT_EQ(sb.GetBufferLength(), (size_t)5);
    ASSERT_EQ(sb.SliceCount(), (size_t)1);
}
TEST(SliceBufferTest, add_rvalue) {
    SliceBuffer sb;
    sb.AddSlice(Slice("hello"));
    ASSERT_EQ(sb.GetBufferLength(), (size_t)5);
    ASSERT_EQ(sb.SliceCount(), (size_t)1);
}
TEST(SliceBufferTest, add_multiple) {
    SliceBuffer sb;
    sb.AddSlice(Slice("aaa"));
    sb.AddSlice(Slice("bbb"));
    sb.AddSlice(Slice("ccc"));
    ASSERT_EQ(sb.SliceCount(), (size_t)3);
    ASSERT_EQ(sb.GetBufferLength(), (size_t)9);
}

// MergeFront

TEST(SliceBufferTest, merge_front_one) {
    SliceBuffer sb;
    sb.AddSlice(Slice("aaa"));
    Slice merged = sb.MergeFront(1);
    ASSERT_EQ(merged.size(), (size_t)3);
    ASSERT_TRUE(merged == "aaa");
}
TEST(SliceBufferTest, merge_front_multiple) {
    SliceBuffer sb;
    sb.AddSlice(Slice("aaa"));
    sb.AddSlice(Slice("bbb"));
    sb.AddSlice(Slice("ccc"));
    Slice merged = sb.MergeFront(2);
    ASSERT_EQ(merged.size(), (size_t)6);
    ASSERT_TRUE(merged == "aaabbb");
}

TEST(SliceBufferTest, merge_front_all) {
    SliceBuffer sb;
    sb.AddSlice(Slice("aaa"));
    sb.AddSlice(Slice("bbb"));
    Slice merged = sb.MergeFront(2);
    ASSERT_EQ(merged.size(), (size_t)6);
    ASSERT_TRUE(merged == "aaabbb");
}

// Merge

TEST(SliceBufferTest, merge_all) {
    SliceBuffer sb;
    sb.AddSlice(Slice("aaa"));
    sb.AddSlice(Slice("bbb"));
    Slice merged = sb.Merge();
    ASSERT_EQ(merged.size(), (size_t)6);
    ASSERT_TRUE(merged == "aaabbb");
}

// GetHeader / MoveHeader

TEST(SliceBufferTest, get_header) {
    SliceBuffer sb;
    sb.AddSlice(Slice("hello world"));
    Slice header = sb.GetHeader(5);
    ASSERT_EQ(header.size(), (size_t)5);
    ASSERT_TRUE(header == "hello");
}
TEST(SliceBufferTest, get_header_across_slices) {
    SliceBuffer sb;
    sb.AddSlice(Slice("hel"));
    sb.AddSlice(Slice("lo wo"));
    sb.AddSlice(Slice("rld"));
    Slice header = sb.GetHeader(5);
    ASSERT_EQ(header.size(), (size_t)5);
    ASSERT_TRUE(header == "hello");
}
TEST(SliceBufferTest, move_header) {
    SliceBuffer sb;
    sb.AddSlice(Slice("hello world"));
    ASSERT_TRUE(sb.MoveHeader(6));
    ASSERT_EQ(sb.GetBufferLength(), (size_t)5);
    ASSERT_EQ(sb.SliceCount(), (size_t)1);
    ASSERT_TRUE(sb.Front() == "world");
}

TEST(SliceBufferTest, move_header_across_slices) {
    SliceBuffer sb;
    sb.AddSlice(Slice("aaa"));
    sb.AddSlice(Slice("bbb"));
    sb.AddSlice(Slice("ccc"));
    ASSERT_TRUE(sb.MoveHeader(5));
    ASSERT_EQ(sb.GetBufferLength(), (size_t)4);
    ASSERT_EQ(sb.SliceCount(), (size_t)2);
}

// Front / Back / PopFront / PopBack

TEST(SliceBufferTest, front_back) {
    SliceBuffer sb;
    sb.AddSlice(Slice("first"));
    sb.AddSlice(Slice("last"));
    ASSERT_TRUE(sb.Front() == "first");
    ASSERT_TRUE(sb.Back() == "last");
}
TEST(SliceBufferTest, pop_front) {
    SliceBuffer sb;
    sb.AddSlice(Slice("aaa"));
    sb.AddSlice(Slice("bbb"));
    sb.PopFront();
    ASSERT_EQ(sb.SliceCount(), (size_t)1);
    ASSERT_TRUE(sb.Front() == "bbb");
}
TEST(SliceBufferTest, pop_back) {
    SliceBuffer sb;
    sb.AddSlice(Slice("aaa"));
    sb.AddSlice(Slice("bbb"));
    sb.PopBack();
    ASSERT_EQ(sb.SliceCount(), (size_t)1);
    ASSERT_TRUE(sb.Front() == "aaa");
}

// Indexing

TEST(SliceBufferTest, index_access) {
    SliceBuffer sb;
    sb.AddSlice(Slice("aaa"));
    sb.AddSlice(Slice("bbb"));
    sb.AddSlice(Slice("ccc"));
    ASSERT_TRUE(sb[0] == "aaa");
    ASSERT_TRUE(sb[1] == "bbb");
    ASSERT_TRUE(sb[2] == "ccc");
}

// ClearBuffer

TEST(SliceBufferTest, clear) {
    SliceBuffer sb;
    sb.AddSlice(Slice("aaa"));
    sb.AddSlice(Slice("bbb"));
    sb.ClearBuffer();
    ASSERT_TRUE(sb.Empty());
    ASSERT_EQ(sb.SliceCount(), (size_t)0);
    ASSERT_EQ(sb.GetBufferLength(), (size_t)0);
}

// CopyToBuffer

TEST(SliceBufferTest, copy_to_buffer) {
    SliceBuffer sb;
    sb.AddSlice(Slice("aaa"));
    sb.AddSlice(Slice("bbb"));
    char buf[6] = {};
    size_t copied = sb.CopyToBuffer(buf, sizeof(buf));
    ASSERT_EQ(copied, (size_t)6);
    ASSERT_EQ(memcmp(buf, "aaabbb", 6), 0);
}
