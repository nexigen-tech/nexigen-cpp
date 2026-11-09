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

/// Severity levels in strictly increasing order; `Off` silences everything.
enum class LogLevel : std::uint8_t {
    Verbose, ///< Chatty diagnostics; below typical production threshold.
    Debug,   ///< Developer-facing detail.
    Info,    ///< Normal operational milestones.
    Warning, ///< Something wrong but recoverable; routes to stderr/console.
    Error,   ///< Operation failed; routing sinks still run.
    Fatal,   ///< Last message before a component gives up.
    Off,     ///< Filter everything: the only value no record can reach.
};

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
template <> struct EnumTraits<nexigen::core::LogLevel> {
    struct Entry {
        nexigen::core::LogLevel value;
        std::string_view name;
    };
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

/// Compile-time lowercase name of a level — the exact spelling used by the
/// JSON `"level"` field and console lines. Static storage, allocation-free.
[[nodiscard]] constexpr std::string_view to_string(LogLevel level) noexcept {
    return core::enum_name(level);
}

/// Parse a canonical lowercase level name (the exact to_string spelling).
/// Returns false leaving the out-parameter UNCHANGED otherwise ("Unknown"
/// is a rendering, not an enumerator).
[[nodiscard]] constexpr bool from_string(std::string_view name, LogLevel& out) noexcept {
    return core::enum_value(name, out);
}

/// ADL JSON writer (cold path only): the JSON value is exactly to_string(level).
inline void to_json(nlohmann::json& j, const LogLevel level) {
    core::mandate::enum_to_json(j, level);
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1: non-string
/// values yield nullptr from get_ptr, unknown names leave `out` unchanged.
inline void from_json(const nlohmann::json& j, LogLevel& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::core
