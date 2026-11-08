// Allocation-free harness backing for the nexigen::tracing tests.

#include "tracing_test_support.hpp"

namespace {
// Allocation detector: the C++ free store is re-backed by a fixed static bump
// arena; any operator new anywhere in the process bumps g_alloc_used. 16 MiB
// покрывает несколько провайдеров (включая один default-размера ~10.5 MiB)
// плюс std::jthread/std::string/nlohmann шум всех наборов.
alignas(16) unsigned char g_arena[16 << 20]{};
} // namespace

std::atomic<std::size_t> g_alloc_used{0};

[[noreturn]] void arena_exhausted() {
    std::abort();
}

void* operator new(std::size_t size) {
    const std::size_t rounded = (size + 15U) & ~static_cast<std::size_t>(15U);
    const std::size_t offset = g_alloc_used.fetch_add(rounded, std::memory_order_relaxed);
    if (offset + rounded > sizeof(g_arena)) {
        arena_exhausted();
    }
    return g_arena + offset;
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
