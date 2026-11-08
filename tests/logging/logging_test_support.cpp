// Allocation-free harness backing for the nexigen::logging tests.

#include "logging_test_support.hpp"

namespace {
// Allocation detector (design D18): the C++ free store is re-backed by a
// fixed static bump arena; any operator new anywhere in the process bumps
// g_alloc_used. 1 MiB covers std::jthread stop-state bookkeeping across every
// logger the suite constructs; hot-path code must leave both counters flat.
alignas(16) unsigned char g_arena[1 << 20]{};
} // namespace

std::size_t g_alloc_used = 0;

[[noreturn]] void arena_exhausted() {
    std::abort();
}

void* operator new(std::size_t size) {
    const std::size_t rounded = (size + 15U) & ~static_cast<std::size_t>(15U);
    if (g_alloc_used + rounded > sizeof(g_arena)) {
        arena_exhausted();
    }
    void* p = g_arena + g_alloc_used;
    g_alloc_used += rounded;
    return p;
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* ptr) noexcept {
    static_cast<void>(ptr);
}
void operator delete(void* ptr, std::size_t) noexcept {
    static_cast<void>(ptr);
}
void operator delete[](void* ptr) noexcept {
    static_cast<void>(ptr);
}
void operator delete[](void* ptr, std::size_t) noexcept {
    static_cast<void>(ptr);
}
