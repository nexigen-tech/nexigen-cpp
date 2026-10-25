// nexigen::logging — severity catalogue (design.md D2).
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

#include <cstdint>
#include <string_view>

namespace nexigen::logging {

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

/// Compile-time lowercase name of a level — the exact spelling used by the
/// JSON `"level"` field and console lines. Static storage, allocation-free.
[[nodiscard]] constexpr std::string_view to_string(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Verbose: return "verbose";
        case LogLevel::Debug:   return "debug";
        case LogLevel::Info:    return "info";
        case LogLevel::Warning: return "warning";
        case LogLevel::Error:   return "error";
        case LogLevel::Fatal:   return "fatal";
        case LogLevel::Off:     return "off";
        default:                return "Unknown";
    }
}

} // namespace nexigen::logging
