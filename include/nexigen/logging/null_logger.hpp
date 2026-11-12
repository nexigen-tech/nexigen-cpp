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
// Level storage reuses detail::LevelControl so the object is layout-identical
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
#include <nexigen/logging/detail/format_param.hpp>
#include <nexigen/logging/detail/level_control.hpp>
#include <nexigen/core/log_level.hpp>

namespace nexigen::logging {

/// @brief Zero-cost sink: every record is accepted and discarded, nothing is emitted.
///
/// Keeps the complete `concepts::Logger` call shape compiled in — including the
/// literal format-string type check performed by the `FormatWithLocation`
/// `consteval` conversion — but performs no observable work: no output, no ring,
/// no clock read, no formatting. The only state is a relaxed level cell so
/// composite trees can re-threshold it like any other member.
///
/// Any number of threads may call the record methods concurrently; nothing
/// crosses a thread boundary except the relaxed level cell, which
/// `set_level()`/`level()` read and write as snapshots (an observation, not
/// synchronization). There is no consumer thread and no `dropped()` surface to
/// aggregate, so `flush()` is trivially complete.
///
/// @note Spec `logging/null-logger`, requirement "Conforming no-op logger".
/// @note Spec `logging/null-logger`, requirement "Calls compile away": under
///       `-O1` and above a discarded call leaves no residue in the emitted code.
class NullLogger {
public:
    /// @brief Construct an inert logger whose stored threshold is `Info`.
    constexpr NullLogger() noexcept = default;

    /// @brief Copy construction is deleted; the level cell is not shared.
    NullLogger(const NullLogger&) = delete;
    /// @brief Copy assignment is deleted; the level cell is not shared.
    NullLogger& operator=(const NullLogger&) = delete;

    /// @brief Store the runtime threshold in the relaxed level cell.
    ///
    /// @param new_level Severity to record; calls below it are considered
    ///        filtered, exactly as on any other conforming logger.
    /// @note Spec `logging/null-logger`, requirement "Conforming no-op logger",
    ///       scenario "Level control is still honored".
    void set_level(core::LogLevel new_level) noexcept { levels_.set_level(new_level); }
    /// @brief Read the stored threshold as a relaxed snapshot.
    ///
    /// @return The level last installed through `set_level()`.
    [[nodiscard]] core::LogLevel level() const noexcept { return levels_.level(); }

    /// @brief Accept and discard one record; the shortest possible path.
    ///
    /// The format wrapper and every trailing argument are cast to `void` in pack
    /// order (`((void)args, ...)`), so nothing is rendered and no member is
    /// touched. The first parameter is the severity to ignore: with an output
    /// that can never observe anything, the level cell is not even loaded. The
    /// literal format string is still checked at compile time by the
    /// `FormatWithLocation` conversion before this call is entered.
    ///
    /// @tparam Args Argument types deduced from the trailing values; each must be
    ///         formattable by the literal per `std::format_string<Args...>`.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values discarded unused; never read, never rendered.
    /// @note Spec `logging/null-logger`, requirement "Conforming no-op logger".
    template <class... Args>
    void log(core::LogLevel, detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        static_cast<void>(fmt);
        ((static_cast<void>(args)), ...);
    }

    /// @brief Discard a `Verbose` record without loading the level cell.
    ///
    /// @tparam Args Argument types deduced from the trailing values.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values discarded unrendered.
    template <class... Args>
    void verbose(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(core::LogLevel::Verbose, fmt, std::forward<Args>(args)...);
    }
    /// @brief Discard a `Debug` record without loading the level cell.
    ///
    /// @tparam Args Argument types deduced from the trailing values.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values discarded unrendered.
    template <class... Args>
    void debug(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(core::LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }
    /// @brief Discard an `Info` record without loading the level cell.
    ///
    /// @tparam Args Argument types deduced from the trailing values.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values discarded unrendered.
    template <class... Args>
    void info(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(core::LogLevel::Info, fmt, std::forward<Args>(args)...);
    }
    /// @brief Discard a `Warning` record without loading the level cell.
    ///
    /// @tparam Args Argument types deduced from the trailing values.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values discarded unrendered.
    template <class... Args>
    void warning(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(core::LogLevel::Warning, fmt, std::forward<Args>(args)...);
    }
    /// @brief Discard an `Error` record without loading the level cell.
    ///
    /// @tparam Args Argument types deduced from the trailing values.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values discarded unrendered.
    template <class... Args>
    void error(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(core::LogLevel::Error, fmt, std::forward<Args>(args)...);
    }
    /// @brief Discard a `Fatal` record without loading the level cell.
    ///
    /// @tparam Args Argument types deduced from the trailing values.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values discarded unrendered.
    template <class... Args>
    void fatal(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(core::LogLevel::Fatal, fmt, std::forward<Args>(args)...);
    }

    /// @brief Report the trivially complete control-plane barrier.
    ///
    /// @return An OK `Status`; there is never work to drain.
    /// @note Spec `logging/null-logger`, requirement "Conforming no-op logger":
    ///       `flush` returns OK immediately.
    [[nodiscard]] core::Status flush() noexcept { return {}; }

    /// @brief Render the sink's complete state for cold-path diagnostics.
    ///
    /// @return `NullLogger(level=<lowercase level>)`; the stored threshold is the
    ///         entire state of this sink.
    /// @note Mandate 9 (`to_string`), and spec `logging/null-logger`, requirement
    ///       "NullLogger diagnostic string"; never call this on a hot path.
    [[nodiscard]] std::string to_string() const {
        return "NullLogger(level=" + std::string(core::to_string(levels_.level())) + ")";
    }

private:
    /// @brief Relaxed threshold cell; the only state the sink owns.
    detail::LevelControl levels_{};
};

/// @brief Free helper mirroring `NullLogger::to_string` for generic call sites.
///
/// @param logger Instance to render; may be any level setting.
/// @return The same text as `logger.to_string()`.
/// @note Mandate 9 (`to_string`): the free form keeps ADL/template call sites
///       uniform across the subsystem.
[[nodiscard]] inline std::string to_string(const NullLogger& logger) {
    return logger.to_string();
}

static_assert(concepts::Logger<NullLogger>,
              "NullLogger must satisfy the Logger contract (pinned at header level)");

} // namespace nexigen::logging
