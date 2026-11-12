// nexigen::chrono — chrono-local worker logger concept (change
// add-chrono-clock-service; spec chrono/chrono-provider). Switched onto the
// shared core::LogLevel by change add-clock-authority-core.
#pragma once

#include <concepts>
#include <string_view>

#include <nexigen/core/log_level.hpp>

namespace nexigen::chrono::concepts {

/// @brief A chrono-local logger accepting `void log(core::LogLevel, std::string_view) noexcept`.
///
/// The worker-side contract for daemon diagnostics, deliberately narrower than
/// the full logging module: one noexcept call, no virtual dispatch, no
/// allocation required by the concept. The application bridges a
/// `nexigen::logging` logger to this shape, and the bound logger must outlive
/// the worker that holds it.
///
/// @tparam T Candidate logger type; it must expose
///         `log(core::LogLevel, std::string_view) noexcept` returning `void`.
/// @note Spec `chrono/worker`, requirement "Worker logger binding".
template <class T>
concept Logger = requires(T& logger, core::LogLevel level, std::string_view message) {
    { logger.log(level, message) } noexcept -> std::same_as<void>;
};

} // namespace nexigen::chrono::concepts
