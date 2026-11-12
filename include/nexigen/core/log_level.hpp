// nexigen::core — program-wide severity catalogue (change
// move-log-level-to-core, design D1).
//
// LogLevel is the ordering domain for every logger's threshold: declared
// Verbose..Off so the built-in relational order of the scoped enum *is*
// severity order (Verbose < Debug < ... < Fatal < Off); "Off" being the
// largest value makes "everything filtered" the top-of-range state instead of
// a special case in the filter compare.
//
// uint8_t underlying type: the enum lands inside LogRecord (one byte) and is
// the same shape std::atomic<LogLevel> can hold in one byte — lock-free by
// construction.
//
// HOT PATH: to_string is constexpr over static-storage literals — no
// allocation, no exception path, exactly like nexigen::core::to_string(ErrorCode).
// The `default:` arm keeps a garbage-cast value well-defined ("Unknown");
// every enumerator is pinned by static_assert in the test TU.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::core {

/// @brief Severity levels in strictly increasing order; `Off` silences
///        everything.
///
/// The built-in relational order of the scoped enum is severity order
/// (`Verbose < Debug < ... < Fatal < Off`), so a filter compare needs no lookup
/// table, and `Off` being the largest value makes "everything filtered" the
/// top-of-range state. The `std::uint8_t` underlying type keeps a level one
/// byte wide inside `LogRecord` and atomically loadable.
///
/// @note Spec `core/log-level`, requirement "Severity catalogue".
enum class LogLevel : std::uint8_t {
    Verbose, ///< Chatty diagnostics; below typical production threshold.
    Debug,   ///< Developer-facing detail.
    Info,    ///< Normal operational milestones.
    Warning, ///< Something wrong but recoverable; routes to stderr/console.
    Error,   ///< Operation failed; routing sinks still run.
    Fatal,   ///< Last message before a component gives up.
    Off,     ///< Filter everything: the only value no record can reach.
};

/// @brief Vocabulary table for `LogLevel` (`core::enum_name`/`enum_value`).
///
/// The single source of truth for the lowercase wire names, in severity order.
template <> struct EnumTraits<nexigen::core::LogLevel> {
    /// @brief One table row: an enumerator and its canonical spelling.
    struct Entry {
        nexigen::core::LogLevel value; ///< Enumerator described by the row.
        std::string_view name;         ///< Canonical lowercase wire name.
    };
    /// @brief Name table covering every enumerator, lowest severity first.
    static constexpr std::array<Entry, 7> entries{{
        {nexigen::core::LogLevel::Verbose, "verbose"},
        {nexigen::core::LogLevel::Debug, "debug"},
        {nexigen::core::LogLevel::Info, "info"},
        {nexigen::core::LogLevel::Warning, "warning"},
        {nexigen::core::LogLevel::Error, "error"},
        {nexigen::core::LogLevel::Fatal, "fatal"},
        {nexigen::core::LogLevel::Off, "off"},
    }};
};

/// @brief Compile-time lowercase name of a level.
///
/// The exact spelling used by the JSON `"level"` field and console lines;
/// static storage, allocation-free, no exception path.
///
/// @param level Severity level to render.
/// @return View over the canonical lowercase name; a value outside the table
///         renders `"Unknown"`.
/// @note Spec `core/log-level`, requirement "Compile-time level naming";
///       mandate 9.
[[nodiscard]] constexpr std::string_view to_string(LogLevel level) noexcept {
    return core::enum_name(level);
}

/// @brief Parse a canonical lowercase level name.
///
/// @param name Candidate spelling (the exact `to_string` output).
/// @param out Destination, untouched unless the name matches exactly.
/// @return `true` with the enumerator on an exact, case-sensitive match;
///         `false` otherwise (`"Unknown"` is a rendering, not an enumerator).
/// @note Spec `core/log-level`, requirement "LogLevel name parsing".
[[nodiscard]] constexpr bool from_string(std::string_view name, LogLevel& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer: the value is exactly `to_string(level)`.
///
/// @param j JSON value overwritten with the lowercase name.
/// @param level Level to serialize.
/// @note Spec `core/log-level`, requirement "LogLevel JSON serialization";
///       mandate 10. Cold path only.
inline void to_json(nlohmann::json& j, const LogLevel level) {
    core::mandate::enum_to_json(j, level);
}

/// @brief ADL JSON reader, strictly non-throwing under `JSON_NOEXCEPTION=1`.
///
/// @param j JSON value to read; a non-string value yields no pointer and leaves
///        `out` unchanged.
/// @param out Destination, untouched on a non-string value or unknown name.
/// @note Spec `core/log-level`, requirement "LogLevel JSON serialization";
///       mandate 10. Never aborts.
inline void from_json(const nlohmann::json& j, LogLevel& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::core
