// nexigen::chrono — type-erased worker log hook (change
// add-chrono-clock-service; spec chrono/chrono-provider). Plain function
// pointers, no virtual dispatch, no RTTI. Switched onto the shared
// core::LogLevel by change add-clock-authority-core.
#pragma once

#include <string_view>

#include <nexigen/core/log_level.hpp>

namespace nexigen::chrono::detail {

/// @brief Type-erased worker log hook built from plain function pointers.
///
/// The worker stores a `void*` self handle and one thunk instead of a templated
/// logger or a virtual interface: no vtable, no RTTI, no allocation. The
/// binding is installed before `start()` and the pointed-to logger must outlive
/// the worker; a default-constructed binding (`log == nullptr`) means "no
/// logger" and the worker skips logging.
///
/// @note Spec `chrono/worker`, requirement "Worker logger binding".
struct LogBinding {
    void* self{nullptr}; ///< Opaque logger instance passed back to `log`.
    void (*log)(void*, core::LogLevel, std::string_view) noexcept {nullptr}; ///< Adapter thunk.
};

} // namespace nexigen::chrono::detail
