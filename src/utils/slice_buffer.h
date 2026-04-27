/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_UTILS_SLICE_BUFFER_H_
#define CONNX_SRC_UTILS_SLICE_BUFFER_H_

#include <stddef.h>
#include <stdint.h>
#include <deque>

#include "src/utils/slice.h"

namespace connx {
class SliceBuffer final {
public:
    SliceBuffer();
    ~SliceBuffer();

    Slice Merge() const;
    Slice MergeFront(size_t count) const;
    size_t SliceCount() const;
    size_t GetBufferLength() const;
    void AddSlice(const Slice& s);
    void AddSlice(Slice&& s);

    Slice GetHeader(size_t len);
    bool MoveHeader(size_t len);
    void ClearBuffer();
    bool Empty() const;

    // have assert if empty
    const Slice& Front() const;
    const Slice& Back() const;

    void PopFront();
    void PopBack();

    const Slice& operator[](size_t n) const;
    const Slice& At(size_t n) const;

    size_t CopyToBuffer(void* buff, size_t len);

private:
    size_t length_;
    std::deque<Slice> ds_;
};

} // namespace connx

#endif // CONNX_SRC_UTILS_SLICE_BUFFER_H_
