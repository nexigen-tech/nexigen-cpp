// nexigen::core — the minimal level-aware logger contract (change
// unify-core-concepts; spec core/concepts "Level logger concept").
//
// Deliberately narrower than nexigen::logging::concepts::Logger: one noexcept
// call that takes a level and a rendered message. Workers and services that
// only emit diagnostics can accept this shape without depending on the logging
// module or on another service module.
#pragma once

#include <concepts>
#include <string_view>

#include <nexigen/core/log_level.hpp>

namespace nexigen::core::concepts {

/// @brief A level/message logger accepting one noexcept call.
///
/// The worker-side contract: no virtual dispatch, no formatting machinery, no
/// allocation required by the concept. A richer logger may satisfy it in
/// addition to its own module contract.
///
/// @tparam T Candidate logger type.
/// @note Spec `core/concepts`, requirement "Level logger concept".
template <class T>
concept LevelLogger = requires(T& logger, LogLevel level, std::string_view message) {
    { logger.log(level, message) } noexcept -> std::same_as<void>;
};

} // namespace nexigen::core::concepts
