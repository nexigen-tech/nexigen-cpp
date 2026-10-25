// nexigen::logging — the zero-cost sink (design.md D2/D15, spec logging/null-logger).
//
// NullLogger keeps the full concepts::Logger call shape compiled in but does
// nothing: every record method returns OK immediately without touching the
// level cell, a ring, a clock, or any output. Under -O1+ with the result
// discarded, nothing survives into the emitted code — verified by an objdump
// check documented in the test TU (task 4.1).
//
// Why log() does not even load the level: with an output that can never
// observe anything, every call is already the most aggressive short-circuit
// possible (0 instructions <= the ~1 ns budget, requirement 1). set_level()
// and level() remain functional for API uniformity — composite trees set
// thresholds across all members, and NullLogger answers like everyone else.
//
// Level storage reuses detail::Level_control so the object is layout-identical
// to the async loggers' control cell: swapping sinks in templated call sites
// never changes the memory footprint story.
//
// SPSC CONTRACT (mirrors core::SpscRingBuffer's header): NullLogger owns no
// ring and no consumer thread, so nothing here ever crosses a thread boundary
// except the relaxed level cell — set_level()/level() from any thread are
// SNAPSHOTS (an observation, not synchronization). log()/conveniences/flush()
// carry the same call shape as the async sinks so generic code treats every
// concepts::Logger uniformly.
//
// DROP SEMANTICS (returned Status of log()/conveniences/flush()):
//   {}              the only outcome: with no observable output, nothing can
//                   be filtered, dropped, or throttled — every call succeeds
//   BufferFull      never (this logger owns no ring)
//   RateLimited     never (this logger owns no throttle)
// flush() is trivially complete: it stays in the control-plane vocabulary of
// the concept, and the record methods never block.
#pragma once

#include <source_location>
#include <string_view>

#include <nexigen/core/result.hpp>
#include <nexigen/logging/concepts/logger_concept.hpp>
#include <nexigen/logging/detail/level_control.hpp>
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
    /// possible short-circuit of the filtered path.
    [[nodiscard]] core::Status log(LogLevel, std::source_location,
                                   std::string_view) noexcept {
        return {};
    }

    [[nodiscard]] core::Status verbose(std::string_view msg,
                                       std::source_location loc = std::source_location::current()) noexcept {
        return log(LogLevel::Verbose, loc, msg);
    }
    [[nodiscard]] core::Status debug(std::string_view msg,
                                     std::source_location loc = std::source_location::current()) noexcept {
        return log(LogLevel::Debug, loc, msg);
    }
    [[nodiscard]] core::Status info(std::string_view msg,
                                    std::source_location loc = std::source_location::current()) noexcept {
        return log(LogLevel::Info, loc, msg);
    }
    [[nodiscard]] core::Status warning(std::string_view msg,
                                       std::source_location loc = std::source_location::current()) noexcept {
        return log(LogLevel::Warning, loc, msg);
    }
    [[nodiscard]] core::Status error(std::string_view msg,
                                     std::source_location loc = std::source_location::current()) noexcept {
        return log(LogLevel::Error, loc, msg);
    }
    [[nodiscard]] core::Status fatal(std::string_view msg,
                                     std::source_location loc = std::source_location::current()) noexcept {
        return log(LogLevel::Fatal, loc, msg);
    }

    /// Nothing is pending, so flushing is trivially complete.
    [[nodiscard]] core::Status flush() noexcept { return {}; }

private:
    detail::Level_control levels_{};
};

static_assert(concepts::Logger<NullLogger>,
              "NullLogger must satisfy the Logger contract (pinned at header level)");

} // namespace nexigen::logging
