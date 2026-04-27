/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "src/utils/slice.h"
#include <string.h>
#include <algorithm>
#include "src/utils/atomic.h"
namespace connx {
class SliceRefCount final {
public:
    SliceRefCount();
    ~SliceRefCount() {}

    SliceRefCount(const SliceRefCount&) = delete;
    SliceRefCount& operator=(const SliceRefCount&) = delete;

    void AddRef();
    void DecRef();

private:
    AtomicInt32 refs_;
};

SliceRefCount::SliceRefCount() { refs_.Store(1, MemoryOrder::RELEASE); }

void SliceRefCount::AddRef() { refs_.FetchAdd(1, MemoryOrder::RELAXED); }

void SliceRefCount::DecRef() {
    int32_t n = refs_.FetchSub(1, MemoryOrder::ACQ_REL);
    if (n == 1) {
        free(this);
    }
}

// ------------------------------------------

Slice::Slice(const char* ptr)
    : Slice(ptr, strlen(ptr)) {}
Slice::Slice(const void* buf, size_t len)
    : Slice((const char*)buf, len) {}
Slice::Slice(void* buf, size_t len)
    : Slice((const char*)buf, len) {}
Slice::Slice(const char* ptr, size_t len) {
    if (len == 0) { // empty object
        refs_ = nullptr;
        memset(&data_, 0, sizeof(data_));
        return;
    }

    if (len <= SLICE_INLINED_SIZE) {
        refs_ = nullptr;
        data_.inlined.length = static_cast<uint8_t>(len);
        if (ptr) {
            memcpy(data_.inlined.bytes, ptr, len);
        }
    } else {
        /*  Memory layout used by the slice created here:

            +-----------+----------------------------------------------------------+
            | refcount  | bytes                                                    |
            +-----------+----------------------------------------------------------+

            refcount is a SliceRefCount
            bytes is an array of bytes of the requested length
        */

        refs_ = (SliceRefCount*)malloc(sizeof(SliceRefCount) + len);
        new (refs_) SliceRefCount;
        data_.refcounted.length = len;
        data_.refcounted.bytes = reinterpret_cast<char*>(refs_ + 1);
        if (ptr) {
            memcpy(data_.refcounted.bytes, ptr, len);
        }
    }
}

Slice::Slice() {
    refs_ = nullptr;
    memset(&data_, 0, sizeof(data_));
}

Slice::~Slice() {
    if (refs_) {
        refs_->DecRef();
    }
}

Slice::Slice(const Slice& oth) {
    if (oth.refs_ != nullptr) {
        oth.refs_->AddRef();
    }
    refs_ = oth.refs_;
    data_ = oth.data_;
}

Slice& Slice::operator=(const Slice& oth) {
    if (this != &oth) {
        if (refs_) {
            refs_->DecRef();
        }
        if (oth.refs_) {
            oth.refs_->AddRef();
        }
        refs_ = oth.refs_;
        data_ = oth.data_;
    }
    return *this;
}

Slice::Slice(Slice&& oth)
    : refs_(oth.refs_)
    , data_(oth.data_) {
    oth.refs_ = nullptr;
    oth.data_.refcounted.bytes = nullptr;
    oth.data_.inlined.length = 0;
}

Slice& Slice::operator=(Slice&& oth) {
    if (this != &oth) {
        if (refs_) {
            refs_->DecRef();
        }
        refs_ = oth.refs_;
        data_ = oth.data_;
        oth.refs_ = nullptr;
        oth.data_.refcounted.bytes = nullptr;
        oth.data_.inlined.length = 0;
    }
    return *this;
}

size_t Slice::size() const { return (refs_) ? data_.refcounted.length : data_.inlined.length; }

const char* Slice::begin() const { return (refs_) ? data_.refcounted.bytes : data_.inlined.bytes; }

const char* Slice::end() const { return begin() + size(); }

bool Slice::compare(const char* ptr, size_t len) const {

    if (len != size()) {
        return false;
    }

    return len == 0 || memcmp(begin(), ptr, len) == 0;
}

Slice& Slice::operator+=(const Slice& s) {
    this->operator=(*this + s);
    return *this;
}

void Slice::RemoveTail(size_t remove_size) {
    if (remove_size == 0) {
        return;
    }
    if (remove_size > size()) {
        remove_size = size();
    }

    if (refs_) {
        data_.refcounted.length -= remove_size;
    } else {
        data_.inlined.length -= static_cast<uint8_t>(remove_size);
    }
}

void Slice::RemoveHead(size_t remove_size) {
    if (remove_size == 0) {
        return;
    }

    if (remove_size > size()) {
        remove_size = size();
    }

    if (refs_) {
        data_.refcounted.length -= remove_size;
        data_.refcounted.bytes += remove_size;
    } else {
        data_.inlined.length -= static_cast<uint8_t>(remove_size);
        memmove(data_.inlined.bytes, data_.inlined.bytes + remove_size, data_.inlined.length);
    }
}

void Slice::clear() { this->operator=(Slice()); }

// -----------------------------

Slice MakeSliceDefault() { return MakeSliceByPage(1); }
Slice MakeSliceByPage(size_t page) {
    if (page == 0) return MakeSliceByLength(0);
    return MakeSliceByLength(page * 4096u - sizeof(SliceRefCount));
}
Slice MakeSliceByLength(size_t len) {
    Slice s;
    if (len <= Slice::SLICE_INLINED_SIZE) {
        s.refs_ = nullptr;
        s.data_.inlined.length = static_cast<uint8_t>(len);
    } else {
        s.refs_ = (SliceRefCount*)malloc(sizeof(SliceRefCount) + len);
        new (s.refs_) SliceRefCount;
        s.data_.refcounted.length = len;
        s.data_.refcounted.bytes = reinterpret_cast<char*>(s.refs_ + 1);
    }
    return s;
}

Slice operator+(Slice s1, Slice s2) {
    if (s1.empty()) return s2;
    if (s2.empty()) return s1;

    size_t len = s1.size() + s2.size();
    Slice s = MakeSliceByLength(len);
    auto buff = s.buffer();
    if (!s1.empty()) {
        memcpy(buff, s1.data(), s1.size());
        buff += s1.size();
    }
    if (!s2.empty()) {
        memcpy(buff, s2.data(), s2.size());
    }
    return s;
}
} // namespace connx
