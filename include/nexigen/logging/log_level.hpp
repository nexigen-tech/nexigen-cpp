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
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

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

/// Parse a canonical lowercase level name (the exact to_string spelling).
/// Returns false leaving the out-parameter UNCHANGED otherwise ("Unknown"
/// is a rendering, not an enumerator).
[[nodiscard]] constexpr bool from_string(std::string_view name, LogLevel& out) noexcept {
    if (name == "verbose") { out = LogLevel::Verbose; return true; }
    if (name == "debug")   { out = LogLevel::Debug; return true; }
    if (name == "info")    { out = LogLevel::Info; return true; }
    if (name == "warning") { out = LogLevel::Warning; return true; }
    if (name == "error")   { out = LogLevel::Error; return true; }
    if (name == "fatal")   { out = LogLevel::Fatal; return true; }
    if (name == "off")     { out = LogLevel::Off; return true; }
    return false;
}

/// ADL JSON writer (cold path only): the JSON value is exactly to_string(level).
inline void to_json(nlohmann::json& j, const LogLevel level) {
    j = std::string(to_string(level));
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1: non-string
/// values yield nullptr from get_ptr, unknown names leave `out` unchanged.
inline void from_json(const nlohmann::json& j, LogLevel& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

} // namespace nexigen::logging

/// LogLevel formats as its canonical lowercase name (mandate 11): parse and
/// fill/align handled by the string_view formatter; context templated because
/// libstdc++ 15 evaluates formattability against a substituted context.
template <>
struct std::formatter<nexigen::logging::LogLevel> : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::logging::LogLevel level, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::logging::to_string(level), ctx);
    }
};
