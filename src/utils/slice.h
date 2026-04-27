/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_SRC_UTILS_SLICE_H_
#define CONNX_SRC_UTILS_SLICE_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <ostream>

namespace connx {
class SliceRefCount;

class Slice final {
public:
    Slice();
    Slice(const char* ptr); // size = strlen(ptr)
    Slice(const void* buf, size_t len);
    Slice(void* buf, size_t len);
    Slice(const char* ptr, size_t len);

    ~Slice();

    Slice(const Slice& oth);
    Slice& operator=(const Slice& oth);

    Slice(Slice&& oth);
    Slice& operator=(Slice&& oth);

    size_t size() const;
    const char* begin() const;
    const char* end() const;

    size_t length() const { return size(); }
    bool empty() const { return (size() == 0); }
    const char* data() const { return begin(); }
    char* buffer() const { return const_cast<char*>(begin()); }

    void RemoveHead(size_t bytes);
    void RemoveTail(size_t bytes);

    bool compare(const char* ptr, size_t len) const;
    bool operator==(const Slice& s) const { return compare(s.data(), s.size()); }
    bool operator!=(const Slice& s) const { return !compare(s.data(), s.size()); }

    Slice& operator+=(const Slice& s);
    void clear();

private:
    enum : int { SLICE_INLINED_SIZE = 23 };

    SliceRefCount* refs_;
    union slice_data {
        struct {
            size_t length;
            char* bytes;
        } refcounted;
        struct {
            uint8_t length;
            char bytes[SLICE_INLINED_SIZE];
        } inlined;
    } data_;

    friend class SliceBuffer;
    friend Slice MakeSliceByLength(size_t len);
};

// allocate 1 page
Slice MakeSliceDefault();
// 1 page = 4096 bytes
Slice MakeSliceByPage(size_t page);
Slice MakeSliceByLength(size_t len);

// Merge two slice, s1 in front and s2 in the back
Slice operator+(Slice s1, Slice s2);

static inline std::string SliceToString(const Slice& s) { return std::string(s.data(), s.size()); }
static inline Slice SliceFromString(const std::string& s) { return Slice(s.data(), s.size()); }
static inline std::ostream& operator<<(std::ostream& os, const Slice& s) {
    os << std::string(s.data(), s.size());
    return os;
}
static inline bool operator==(const std::string& str, const Slice& s) {
    return s.compare(str.data(), str.size());
}
static inline bool operator==(const Slice& s, const std::string& str) {
    return s.compare(str.data(), str.size());
}
static inline bool operator==(const char* ptr, const Slice& s) {
    if (ptr == NULL) return s.empty();
    return s.compare(ptr, strlen(ptr));
}
static inline bool operator==(const Slice& s, const char* ptr) {
    if (ptr == NULL) return s.empty();
    return s.compare(ptr, strlen(ptr));
}
} // namespace connx

#endif // CONNX_SRC_UTILS_SLICE_H_
