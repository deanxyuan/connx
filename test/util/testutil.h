/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_TEST_TESTUTIL_H_
#define CONNX_TEST_TESTUTIL_H_

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <sstream>
#include <type_traits>

namespace test {

class StringView {
private:
    const char* data_;
    size_t size_;

public:
    StringView(const char* str)
        : data_(str)
        , size_(str ? strlen(str) : 0) {}

    StringView(const std::string& str)
        : data_(str.data())
        , size_(str.size()) {}

    template <size_t N>
    StringView(const char (&str)[N])
        : data_(str)
        , size_(N - 1) {}

    ~StringView() = default;

    const char* data() const { return data_; }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    std::string to_string() const { return data_ ? std::string(data_, size_) : std::string(); }
};

static inline std::ostream& operator<<(std::ostream& os, const StringView& sv) {
    if (sv.data()) {
        os.write(sv.data(), sv.size());
    } else {
        os << "(null)"; // nullptr
    }
    return os;
}

static inline int CompareStringView(const StringView& lhs, const StringView& rhs) {
    if (!lhs.data() && !rhs.data()) return 0;
    if (!lhs.data()) return -1;
    if (!rhs.data()) return 1;

    size_t bytes = std::min(lhs.size(), rhs.size());
    int result = memcmp(lhs.data(), rhs.data(), bytes);
    if (result == 0) {
        if (lhs.size() < rhs.size()) return -1;
        if (lhs.size() > rhs.size()) return 1;
    }
    return result;
}

static inline bool operator<(const StringView& lhs, const StringView& rhs) {
    return CompareStringView(lhs, rhs) < 0;
}
static inline bool operator<=(const StringView& lhs, const StringView& rhs) {
    return CompareStringView(lhs, rhs) <= 0;
}
static inline bool operator>(const StringView& lhs, const StringView& rhs) {
    return CompareStringView(lhs, rhs) > 0;
}
static inline bool operator>=(const StringView& lhs, const StringView& rhs) {
    return CompareStringView(lhs, rhs) >= 0;
}
static inline bool operator==(const StringView& lhs, const StringView& rhs) {
    return CompareStringView(lhs, rhs) == 0;
}
static inline bool operator!=(const StringView& lhs, const StringView& rhs) {
    return CompareStringView(lhs, rhs) != 0;
}

// for char*/const char*/char[]
template <typename T>
inline bool operator==(const T& lhs, const StringView& rhs) {
    return CompareStringView(StringView(lhs), rhs) == 0;
}

template <typename T>
inline bool operator!=(const T& lhs, const StringView& rhs) {
    return CompareStringView(StringView(lhs), rhs) != 0;
}

template <typename T>
inline bool operator==(const StringView& lhs, const T& rhs) {
    return CompareStringView(lhs, StringView(rhs)) == 0;
}

template <typename T>
inline bool operator!=(const StringView& lhs, const T& rhs) {
    return CompareStringView(lhs, StringView(rhs)) != 0;
}

// string type check
template <typename T>
struct IsStringType : std::false_type {};
template <>
struct IsStringType<char*> : std::true_type {};
template <>
struct IsStringType<const char*> : std::true_type {};
template <size_t N>
struct IsStringType<char[N]> : std::true_type {};
template <size_t N>
struct IsStringType<const char[N]> : std::true_type {};
template <>
struct IsStringType<std::string> : std::true_type {};
template <>
struct IsStringType<const std::string> : std::true_type {};
template <>
struct IsStringType<std::string&> : std::true_type {};
template <>
struct IsStringType<const std::string&> : std::true_type {};

template <typename T>
auto ToComparable(const T& value) ->
    typename std::enable_if<IsStringType<T>::value, StringView>::type {
    return StringView(value);
}
template <typename T>
auto ToComparable(const T& value) ->
    typename std::enable_if<!IsStringType<T>::value, const T&>::type {
    return value;
}

// Run all unit tests registered by the TEST/TEST_F macro.
// Returns 0 if all tests pass.
// Dies or returns a non-zero value if some test fails.
int RunAllTests();

// An instance of Tester is allocated to hold temporary state during
// the execution of an assertion.
class Tester {
private:
    bool ok_;
    const char* fname_;
    int line_;
    std::stringstream ss_;

public:
    Tester(const char* f, int l)
        : ok_(true)
        , fname_(f)
        , line_(l) {}

    ~Tester() {
        if (!ok_) {
            fprintf(stderr, "%s:%d:%s\n", fname_, line_, ss_.str().c_str());
            exit(1);
        }
    }

    Tester& Is(bool b, const char* msg) {
        if (!b) {
            ss_ << " Assertion failure " << msg;
            ok_ = false;
        }
        return *this;
    }

#define BINARY_OP(name, op)                                                                        \
    template <class X, class Y>                                                                    \
    Tester& name(const X& x, const Y& y) {                                                         \
        auto&& lhs = ToComparable(x);                                                              \
        auto&& rhs = ToComparable(y);                                                              \
        if (!(lhs op rhs)) {                                                                       \
            ss_ << " failed: " << lhs << (" " #op " ") << rhs;                                     \
            ok_ = false;                                                                           \
        }                                                                                          \
        return *this;                                                                              \
    }

    BINARY_OP(IsEq, ==)
    BINARY_OP(IsNe, !=)
    BINARY_OP(IsGe, >=)
    BINARY_OP(IsGt, >)
    BINARY_OP(IsLe, <=)
    BINARY_OP(IsLt, <)
#undef BINARY_OP

    // Attach the specified value to the error message if an error has occurred
    template <class V>
    Tester& operator<<(const V& value) {
        if (!ok_) {
            ss_ << " " << value;
        }
        return *this;
    }
};

#define ASSERT_TRUE(c)  ::test::Tester(__FILE__, __LINE__).Is((c), #c)
#define ASSERT_OK(s)    ::test::Tester(__FILE__, __LINE__).IsOk((s))
#define ASSERT_EQ(a, b) ::test::Tester(__FILE__, __LINE__).IsEq((a), (b))
#define ASSERT_NE(a, b) ::test::Tester(__FILE__, __LINE__).IsNe((a), (b))
#define ASSERT_GE(a, b) ::test::Tester(__FILE__, __LINE__).IsGe((a), (b))
#define ASSERT_GT(a, b) ::test::Tester(__FILE__, __LINE__).IsGt((a), (b))
#define ASSERT_LE(a, b) ::test::Tester(__FILE__, __LINE__).IsLe((a), (b))
#define ASSERT_LT(a, b) ::test::Tester(__FILE__, __LINE__).IsLt((a), (b))

#define TCONCAT(a, b)  TCONCAT1(a, b)
#define TCONCAT1(a, b) a##b

// Register the specified test.  Typically not used directly, but
// invoked via the macro expansion of TEST or TEST_F.
bool RegisterTest(const char* base, const char* name, void (*func)());

/**
 * example:
 *   TEST("Common", "1.1-xxxxx")
 *   {
 *       ASSERT_TRUE(...);
 *   }
 */
#define TEST(TestSuit, TestName)                                                                   \
    class TCONCAT(_Test_, __LINE__) {                                                              \
    public:                                                                                        \
        void _Run();                                                                               \
        static void _RunIt() {                                                                     \
            TCONCAT(_Test_, __LINE__) t;                                                           \
            t._Run();                                                                              \
        }                                                                                          \
    };                                                                                             \
    bool TCONCAT(_Test_ignored_, __LINE__) =                                                       \
        ::test::RegisterTest(#TestSuit, #TestName, &TCONCAT(_Test_, __LINE__)::_RunIt);            \
    void TCONCAT(_Test_, __LINE__)::_Run()

// Test fixture interface
class TestFixture {
public:
    virtual ~TestFixture() = default;
    virtual void SetUp() {}
    virtual void TearDown() {}
};

/**
 * @example:
 *  TEST_F(FixtureClass, "1.2-testxxxx")
 *  {
 *      ASSERT_XX(...);
 *  }
 */
#define TEST_F(FixtureClass, TestName)                                                             \
    class TCONCAT(_Test_, __LINE__)                                                                \
        : public TestFixture {                                                                     \
    public:                                                                                        \
        void _Run();                                                                               \
        static void _RunIt() {                                                                     \
            TCONCAT(_Test_, __LINE__) t;                                                           \
            t.SetUp();                                                                             \
            t._Run();                                                                              \
            t.TearDown();                                                                          \
        }                                                                                          \
    };                                                                                             \
    bool TCONCAT(_Test_ignored_, __LINE__) =                                                       \
        ::test::RegisterTest(#FixtureClass, #TestName, &TCONCAT(_Test_, __LINE__)::_RunIt);        \
    void TCONCAT(_Test_, __LINE__)::_Run()

#define RUN_ALL_TESTS()                                                                            \
    int main() { return ::test::RunAllTests(); }

} // namespace test

#endif // CONNX_TEST_TESTUTIL_H_
