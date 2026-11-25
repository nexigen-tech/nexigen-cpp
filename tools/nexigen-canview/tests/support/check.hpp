// Shared assertion helper for the nexigen-canview headless tests.
#pragma once

#include <cstdio>

namespace canview_test {

inline int failures = 0;

inline void check(bool condition, const char* text, int line) {
    if (!condition) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, text);
        ++failures;
    }
}

} // namespace canview_test

#define NX_CHECK(condition) ::canview_test::check((condition), #condition, __LINE__)
