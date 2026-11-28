// Shared harness implementation for the nexigen::canopen::observability tests.
#include "observability_test_support.hpp"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <format>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace {

/// @brief Armed flag for the allocation counter (constant-initialized).
std::atomic<bool> g_allocation_armed{false};
/// @brief Allocations observed while armed.
std::atomic<std::uint64_t> g_allocation_count{0};

} // namespace

void allocation_counter_arm() noexcept {
    g_allocation_count.store(0U, std::memory_order_relaxed);
    g_allocation_armed.store(true, std::memory_order_release);
}

std::uint64_t allocation_counter_disarm() noexcept {
    g_allocation_armed.store(false, std::memory_order_release);
    return g_allocation_count.load(std::memory_order_relaxed);
}

void* operator new(std::size_t size) {
    if (g_allocation_armed.load(std::memory_order_relaxed)) {
        g_allocation_count.fetch_add(1U, std::memory_order_relaxed);
    }
    void* memory = std::malloc(size == 0U ? 1U : size);
    if (memory == nullptr) {
        std::abort();
    }
    return memory;
}

void* operator new[](std::size_t size) {
    if (g_allocation_armed.load(std::memory_order_relaxed)) {
        g_allocation_count.fetch_add(1U, std::memory_order_relaxed);
    }
    void* memory = std::malloc(size == 0U ? 1U : size);
    if (memory == nullptr) {
        std::abort();
    }
    return memory;
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    if (g_allocation_armed.load(std::memory_order_relaxed)) {
        g_allocation_count.fetch_add(1U, std::memory_order_relaxed);
    }
    return std::malloc(size == 0U ? 1U : size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    if (g_allocation_armed.load(std::memory_order_relaxed)) {
        g_allocation_count.fetch_add(1U, std::memory_order_relaxed);
    }
    return std::malloc(size == 0U ? 1U : size);
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}
void operator delete[](void* memory) noexcept {
    std::free(memory);
}
void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}
void operator delete[](void* memory, std::size_t) noexcept {
    std::free(memory);
}
void operator delete(void* memory, const std::nothrow_t&) noexcept {
    std::free(memory);
}
void operator delete[](void* memory, const std::nothrow_t&) noexcept {
    std::free(memory);
}

namespace {

std::vector<CapturedRecord>& records() {
    static std::vector<CapturedRecord> storage;
    return storage;
}

} // namespace

std::vector<CapturedRecord>& captured_records() noexcept {
    return records();
}

std::mutex& captured_mutex() noexcept {
    static std::mutex mutex;
    return mutex;
}

void clear_captured_records() noexcept {
    std::lock_guard<std::mutex> guard{captured_mutex()};
    records().clear();
}

bool captured_contains(std::string_view needle) noexcept {
    return captured_count(needle) != 0U;
}

std::size_t captured_count(std::string_view needle) noexcept {
    std::lock_guard<std::mutex> guard{captured_mutex()};
    std::size_t matches = 0U;
    for (const CapturedRecord& record : records()) {
        if (record.text.find(needle) != std::string::npos) {
            ++matches;
        }
    }
    return matches;
}

std::size_t captured_level_count(nexigen::core::LogLevel level) noexcept {
    std::lock_guard<std::mutex> guard{captured_mutex()};
    std::size_t matches = 0U;
    for (const CapturedRecord& record : records()) {
        if (record.level == level) {
            ++matches;
        }
    }
    return matches;
}
