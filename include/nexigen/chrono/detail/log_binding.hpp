// nexigen::chrono — type-erased worker log hook (change
// add-chrono-clock-service; spec chrono/chrono-provider). Plain function
// pointers, no virtual dispatch, no RTTI. Switched onto the shared
// core::LogLevel by change add-clock-authority-core.
#pragma once

#include <string_view>

#include <nexigen/core/log_level.hpp>

namespace nexigen::chrono::detail {

/// Type-erased worker log hook: plain function pointers, no virtual dispatch.
struct LogBinding {
    void* self{nullptr};
    void (*log)(void*, core::LogLevel, std::string_view) noexcept {nullptr};
};

} // namespace nexigen::chrono::detail
