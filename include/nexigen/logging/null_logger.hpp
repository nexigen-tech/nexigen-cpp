// nexigen::logging — the zero-cost sink (design D2/D15; format rework design
// D3/D10; spec logging/null-logger).
//
// NullLogger keeps the full concepts::Logger call shape compiled in but does
// nothing: every record method returns immediately without touching the level
// cell, a ring, a clock, or any output. Under -O1+ with the result discarded,
// nothing survives into the emitted code — verified by an objdump check
// documented in the test TU. The literal format string is still type-checked
// (the FormatWithLocation consteval conversion happens before the call), so
// "compile the call shape" means "compile the format errors" too.
//
// Why log() does not even load the level: with an output that can never
// observe anything, every call is already the most aggressive short-circuit
// possible. set_level() and level() remain functional for API uniformity —
// composite trees set thresholds across all members, and NullLogger answers
// like everyone else.
//
// Level storage reuses detail::Level_control so the object is layout-identical
// to the async loggers' control cell: swapping sinks in templated call sites
// never changes the memory footprint story.
//
// MULTI-PRODUCER CONTRACT: NullLogger owns no rings and no consumer thread,
// so any number of threads may call record methods; nothing crosses a thread
// boundary except the relaxed level cell — set_level()/level() from any
// thread are SNAPSHOTS (an observation, not synchronization).
//
// DROP SEMANTICS (record methods return void): with no observable output,
// nothing can be filtered, dropped, or throttled — every call completes
// silently and dropped() is permanently zero. flush() is trivially complete.
#pragma once

#include <format>
#include <string>
#include <utility>

#include <nexigen/core/result.hpp>
#include <nexigen/logging/concepts/logger_concept.hpp>
#include <nexigen/logging/detail/level_control.hpp>
#include <nexigen/logging/format_with_location.hpp>
#include <nexigen/logging/log_level.hpp>

namespace nexigen::logging {

/// No-op logger: all operations succeed, nothing is emitted.
class NullLogger {
public:
    constexpr NullLogger() noexcept = default;

    NullLogger(const NullLogger&) = delete;
    NullLogger& operator=(const NullLogger&) = delete;

    /// Level control is stored (relaxed cell) so the API behaves uniformly.
    void set_level(LogLevel new_level) noexcept { levels_.set_level(new_level); }
    [[nodiscard]] LogLevel level() const noexcept { return levels_.level(); }

    /// Every record call is a successful no-op — by definition the shortest
    /// possible path. The pack is discarded without rendering anything.
    template <class... Args>
    void log(LogLevel, detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        static_cast<void>(fmt);
        ((static_cast<void>(args)), ...);
    }

    template <class... Args>
    void verbose(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(LogLevel::Verbose, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void debug(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void info(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(LogLevel::Info, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void warning(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(LogLevel::Warning, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void error(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(LogLevel::Error, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void fatal(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(LogLevel::Fatal, fmt, std::forward<Args>(args)...);
    }

    /// Nothing is pending, so flushing is trivially complete.
    [[nodiscard]] core::Status flush() noexcept { return {}; }

    /// Cold-path diagnostic (mandate 9): the stored level is the complete
    /// state of this sink.
    [[nodiscard]] std::string to_string() const {
        return "NullLogger(level=" + std::string(logging::to_string(levels_.level())) + ")";
    }

private:
    detail::Level_control levels_{};
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const NullLogger& logger) {
    return logger.to_string();
}

static_assert(concepts::Logger<NullLogger>,
              "NullLogger must satisfy the Logger contract (pinned at header level)");

} // namespace nexigen::logging
