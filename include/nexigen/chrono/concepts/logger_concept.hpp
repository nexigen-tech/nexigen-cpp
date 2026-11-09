// nexigen::chrono — chrono-local worker logger concept (change
// add-chrono-clock-service; spec chrono/chrono-provider). Switched onto the
// shared core::LogLevel by change add-clock-authority-core.
#pragma once

#include <concepts>
#include <string_view>

#include <nexigen/core/log_level.hpp>

namespace nexigen::chrono::concepts {

/// A chrono-local logger: anything with a void log(core::LogLevel,
/// std::string_view) noexcept member. The application bridges a
/// nexigen::logging logger to this contract.
template <class T>
concept Logger = requires(T& logger, core::LogLevel level, std::string_view message) {
    { logger.log(level, message) } noexcept -> std::same_as<void>;
};

} // namespace nexigen::chrono::concepts
