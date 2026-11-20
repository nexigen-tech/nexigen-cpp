// nexigen::core — the full asynchronous-logger contract (change
// unify-core-concepts; spec core/concepts "Logger concept").
//
// The format wrapper is a template parameter supplied by the logger module, so
// the concept checks the real `log(LogLevel, Format<Args...>, Args&&...)` and
// convenience signatures instead of only literal call shapes, while core names
// no logging type.
#pragma once

#include <concepts>
#include <utility>

#include <nexigen/core/log_level.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::core::concepts {

/// @brief Structural contract for an asynchronous logger.
///
/// Requires runtime threshold control, the explicit-level variadic entry, the
/// six severity conveniences, and the control-plane flush barrier. Record
/// members are `noexcept` and return `void`; only `flush()` returns
/// `core::Status`.
///
/// @tparam T Candidate logger type.
/// @tparam Format Format-wrapper template the logger accepts, for example
///         `nexigen::logging::FormatWithLocation`.
/// @note Spec `core/concepts`, requirement "Logger concept".
/// @note Spec `logging/logger-concept`, requirement "Logger concept core
///       surface" and requirement "Logger concept convenience methods".
template <class T, template <class...> class Format>
concept Logger = requires(T t, const T ct, LogLevel level) {
    { t.set_level(level) } noexcept -> std::same_as<void>;
    { ct.level() } noexcept -> std::same_as<LogLevel>;

    { t.log(level, std::declval<Format<>>()) } noexcept -> std::same_as<void>;
    { t.log(level, std::declval<Format<int>>(), 1) } noexcept -> std::same_as<void>;

    { t.verbose(std::declval<Format<>>()) } noexcept -> std::same_as<void>;
    { t.debug(std::declval<Format<>>()) } noexcept -> std::same_as<void>;
    { t.info(std::declval<Format<>>()) } noexcept -> std::same_as<void>;
    { t.warning(std::declval<Format<>>()) } noexcept -> std::same_as<void>;
    { t.error(std::declval<Format<>>()) } noexcept -> std::same_as<void>;
    { t.fatal(std::declval<Format<>>()) } noexcept -> std::same_as<void>;
    { t.info(std::declval<Format<int>>(), 1) } noexcept -> std::same_as<void>;

    { t.flush() } noexcept -> std::same_as<Status>;
};

} // namespace nexigen::core::concepts
