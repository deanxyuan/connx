/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "src/utils/slice_buffer.h"
#include <string.h>
#include <iterator>
#include "src/utils/useful.h"

namespace connx {

SliceBuffer::SliceBuffer()
    : length_(0) {}
SliceBuffer::~SliceBuffer() {}

Slice SliceBuffer::Merge() const {

    if (SliceCount() == 0) {
        return Slice();
    }

    if (SliceCount() == 1) {
        return ds_[0];
    }

    size_t len = GetBufferLength();
    Slice ret = MakeSliceByLength(len);
    char* buf = ret.buffer();
    for (auto it = ds_.begin(); it != ds_.end(); ++it) {
        size_t blk_size = it->size();
        memcpy(buf, it->begin(), blk_size);
        buf += blk_size;
    }
    return ret;
}
size_t SliceBuffer::SliceCount() const { return ds_.size(); }
size_t SliceBuffer::GetBufferLength() const { return length_; }
void SliceBuffer::ClearBuffer() {
    ds_.clear();
    length_ = 0;
}
bool SliceBuffer::Empty() const { return ds_.empty(); }

Slice SliceBuffer::MergeFront(size_t count) const {
    if (count == 0 || ds_.empty()) {
        return Slice();
    }
    if (count == 1) {
        return ds_[0];
    }

    size_t n = count < ds_.size() ? count : ds_.size();
    size_t total = 0;
    for (size_t i = 0; i < n; i++) {
        total += ds_[i].size();
    }
    Slice ret = MakeSliceByLength(total);
    char* buf = ret.buffer();
    for (size_t i = 0; i < n; i++) {
        memcpy(buf, ds_[i].begin(), ds_[i].size());
        buf += ds_[i].size();
    }
    return ret;
}

void SliceBuffer::AddSlice(const Slice& s) {
    if (s.empty()) return;
    ds_.push_back(s);
    length_ += s.size();
}

void SliceBuffer::AddSlice(Slice&& s) {
    if (s.empty()) return;
    ds_.emplace_back(s);
    length_ += s.size();
}

Slice SliceBuffer::GetHeader(size_t len) {
    if (len == 0 || GetBufferLength() < len) {
        return Slice();
    }

    Slice s = MakeSliceByLength(len);
    CopyToBuffer(s.buffer(), len);
    return s;
}

bool SliceBuffer::MoveHeader(size_t len) {
    if (GetBufferLength() < len) {
        return false;
    }
    if (len == 0) {
        return true;
    }

    auto it = ds_.begin();
    size_t left = it->size();

    if (left > len) {
        it->RemoveHead(len);
        length_ -= len;
    } else if (left == len) {
        length_ -= len;
        ds_.erase(it);
    } else {
        // len > left
        length_ -= left;
        ds_.erase(it);

        return MoveHeader(len - left);
    }
    return true;
}

size_t SliceBuffer::CopyToBuffer(void* buffer, size_t length) {
    CONNX_ASSERT(length <= GetBufferLength());

    auto it = ds_.begin();

    size_t left = length;
    size_t pos = 0;

    while (it != ds_.end() && left != 0) {

        size_t len = CONNX_MIN(left, it->size());
        memcpy((uint8_t*)buffer + pos, it->begin(), len);

        left -= len;
        pos += len;

        ++it;
    }

    return pos;
}

void SliceBuffer::PopFront() {
    CONNX_ASSERT(!ds_.empty());
    length_ -= ds_.front().size();
    ds_.pop_front();
}

void SliceBuffer::PopBack() {
    CONNX_ASSERT(!ds_.empty());
    length_ -= ds_.back().size();
    ds_.pop_back();
}

const Slice& SliceBuffer::Front() const { return ds_.front(); }

const Slice& SliceBuffer::Back() const { return ds_.back(); }

const Slice& SliceBuffer::At(size_t n) const { return ds_.at(n); }

const Slice& SliceBuffer::operator[](size_t n) const { return ds_[n]; }

} // namespace connx
