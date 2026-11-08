// nexigen::chrono — type-erased worker log hook (change
// add-chrono-clock-service; spec chrono/chrono-provider). Plain function
// pointers, no virtual dispatch, no RTTI.
#pragma once

#include <string_view>

#include <nexigen/chrono/clock_log_level.hpp>

namespace nexigen::chrono::detail {

/// Type-erased worker log hook: plain function pointers, no virtual dispatch.
struct LogBinding {
    void* self{nullptr};
    void (*log)(void*, ClockLogLevel, std::string_view) noexcept {nullptr};
};

} // namespace nexigen::chrono::detail
