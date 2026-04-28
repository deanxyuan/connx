/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "test/util/testutil.h"

#include <vector>

namespace test {

namespace {
struct Test {
    const char* base;
    const char* name;
    void (*func)();
};
std::vector<Test>* tests;
} // namespace

bool RegisterTest(const char* base, const char* name, void (*func)()) {
    if (tests == nullptr) {
        tests = new std::vector<Test>;
    }
    Test t;
    t.base = base;
    t.name = name;
    t.func = func;
    tests->push_back(t);
    return true;
}

int RunAllTests() {
    int num = 0;
    if (tests != nullptr) {
        for (size_t i = 0; i < tests->size(); i++) {
            const Test& t = (*tests)[i];
            fprintf(stderr, "==== Test %s.%s\n", t.base, t.name);
            (*t.func)();
            ++num;
        }
    }
    fprintf(stderr, "==== PASSED %d tests\n", num);
    return 0;
}

} // namespace test
