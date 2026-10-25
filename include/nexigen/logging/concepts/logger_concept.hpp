// nexigen::logging — the Logger contract (design.md D3).
//
// concepts::Logger is the structural C++23 contract every concrete logger in
// this subsystem satisfies: NullLogger, ConsoleLogger, FileLogger and
// CompositeLogger<...> all conform, pinned by static_assert in their headers'
// own tests — conformance is compile law, not documentation. There is no
// polymorphic base class anywhere in nexigen::logging: the concept is how
// heterogeneous loggers interoperate without virtual dispatch.
//
// The convenience members (verbose..fatal) are constrained on the
// message-only call shape `{ t.info(msg) }`: a candidate whose overloads all
// demand an explicit std::source_location does NOT satisfy the concept. The
// defaulted `std::source_location loc = std::source_location::current()`
// parameter is therefore part of the contract — records carry the caller's
// call-site, never the library's internals.
//
// CONTRACT for implementers of the asynchronous sinks (mirrors
// core::SpscRingBuffer's header): exactly ONE producer thread calls
// log()/conveniences on a given instance, and the logger's own worker thread
// is the sole consumer; any other topology is outside the contract.
// set_level()/level() (and dropped()/pending()/writer_*() where present) are
// safe from third threads because they read relaxed SNAPSHOTS — an
// observation, not synchronization. flush() is a control-plane drain barrier
// and sits OUTSIDE the never-block guarantee that binds the record methods.
//
// DROP SEMANTICS (the Status returned by log()/conveniences):
//   {}              record accepted into the ring, OR filtered below the
//                   active level (filtering is normal operation, not a loss)
//   BufferFull      ring full: nothing enqueued, counted in dropped()
//   RateLimited     throttle budget spent for the window, counted in
//                   dropped(); a drop-summary record reports exact counts
// NullLogger returns {} for everything (no ring, no throttle);
// CompositeLogger surfaces the LAST member failure across its fold.
#pragma once

#include <concepts>
#include <source_location>
#include <string_view>

#include <nexigen/core/result.hpp>
#include <nexigen/logging/log_level.hpp>

namespace nexigen::logging::concepts {

/// Structural requirements of a nexigen logger. All members noexcept, all
/// outcomes reported through nexigen::core::Status, no virtual anywhere.
template <class T>
concept Logger = requires(T t, const T ct, LogLevel lv,
                          std::source_location loc, std::string_view msg) {
    /// Runtime threshold control (see detail::Level_control, design D2).
    { t.set_level(lv) } noexcept -> std::same_as<void>;
    { ct.level() } noexcept -> std::same_as<LogLevel>;

    /// Explicit-level entry point: filter → stamp → enqueue, never blocks.
    { t.log(lv, loc, msg) } noexcept -> std::same_as<nexigen::core::Status>;

    /// Severity conveniences with defaulted call-site location (message-only
    /// call shape is the requirement — see header).
    { t.verbose(msg) } noexcept -> std::same_as<nexigen::core::Status>;
    { t.debug(msg) }   noexcept -> std::same_as<nexigen::core::Status>;
    { t.info(msg) }    noexcept -> std::same_as<nexigen::core::Status>;
    { t.warning(msg) } noexcept -> std::same_as<nexigen::core::Status>;
    { t.error(msg) }   noexcept -> std::same_as<nexigen::core::Status>;
    { t.fatal(msg) }   noexcept -> std::same_as<nexigen::core::Status>;

    /// Control-plane drain barrier (never a hot-path call — design D8).
    { t.flush() } noexcept -> std::same_as<nexigen::core::Status>;
};

} // namespace nexigen::logging::concepts
