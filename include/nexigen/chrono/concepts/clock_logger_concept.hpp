// nexigen::chrono — chrono-local worker logger concept (change
// add-chrono-clock-service; spec chrono/chrono-provider).
#pragma once

#include <concepts>
#include <string_view>

#include <nexigen/chrono/clock_log_level.hpp>

namespace nexigen::chrono::concepts {

/// A chrono-local logger: anything with a void log(ClockLogLevel,
/// std::string_view) noexcept member. The application bridges a
/// nexigen::logging logger to this contract.
template <class T>
concept ClockLogger = requires(T& logger, ClockLogLevel level, std::string_view message) {
    { logger.log(level, message) } noexcept -> std::same_as<void>;
};

} // namespace nexigen::chrono::concepts
