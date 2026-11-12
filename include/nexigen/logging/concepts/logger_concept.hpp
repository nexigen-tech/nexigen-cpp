// nexigen::logging — the Logger contract (design D3; multi-producer + format
// rework: change add-serialization-and-wait-free-logging, design D6/D10).
//
// concepts::Logger is the structural C++23 contract every concrete logger in
// this subsystem satisfies: NullLogger, ConsoleLogger, FileLogger and
// CompositeLogger<...> all conform, pinned by static_assert in their headers —
// conformance is compile law, not documentation promise. There is no
// polymorphic base class anywhere in nexigen::logging: the concept is how
// heterogeneous loggers interoperate without virtual dispatch.
//
// The convenience members are constrained on the MESSAGE-ONLY literal call
// shape `{ t.info("literal") }`: a candidate whose overloads all demand an
// explicit argument pack or an explicit location does NOT satisfy the
// concept. The implicit FormatWithLocation conversion (consteval constructor
// with std::source_location::current() defaulted) is therefore part of the
// contract — records carry the caller's call-site, never library internals.
//
// Record methods return void (spec logging/logger-concept "Drop reporting
// moves to snapshots"): accepted/filtered/dropped outcomes all complete
// silently; losses are reported through logger-level snapshot surfaces
// (dropped() over the causes rate-limited / buffer-full / slot-exhausted)
// and the in-stream drop-summary record.
//
// CONTRACT for implementers of the asynchronous sinks (mirrors
// logging/multi-producer): up to MaxThreads distinct producer threads may
// call log()/conveniences on a given instance — each thread lands in its own
// SPSC shard, and the logger's own worker thread is the sole consumer of all
// shards. set_level()/level() (and dropped()/pending()/writer_*() where
// present) are safe from third threads because they read relaxed SNAPSHOTS —
// an observation, not synchronization. flush() is a control-plane drain
// barrier and sits OUTSIDE the never-block guarantee that binds the record
// methods.
//
// DROP SEMANTICS (no return values — snapshots only):
//   accepted        record packed into the calling thread's shard ring
//   filtered        below the active level: silent, not counted
//   buffer-full     shard ring full: counted in dropped().buffer_full
//   rate-limited    throttle budget spent: counted in dropped().rate_limited
//   slot-exhausted  beyond MaxThreads producers: counted in
//                   dropped().slot_exhausted
// NullLogger can drop nothing (no rings, no throttle); CompositeLogger
// fans every outcome through to its members independently.
#pragma once

#include <concepts>
#include <format>
#include <source_location>
#include <string_view>

#include <nexigen/core/result.hpp>
#include <nexigen/logging/format_with_location.hpp>
#include <nexigen/core/log_level.hpp>

namespace nexigen::logging::concepts {

/// @brief Structural C++23 contract every `nexigen::logging` sink satisfies.
///
/// A type satisfies `Logger` when it offers runtime threshold control
/// (`set_level`/`level`), the explicit-level variadic entry `log`, the six
/// severity conveniences, and the `flush` control-plane barrier — with no
/// virtual dispatch anywhere. Each concrete sink pins conformance with a
/// `static_assert` in its own header, so the contract is compile law.
///
/// Record members are `noexcept` and return `void`: accepted, filtered and
/// dropped calls are indistinguishable at the call site, and losses are audited
/// through `dropped()` snapshots instead. Only the control plane (`flush`)
/// reports through `nexigen::core::Status`.
///
/// The message-only literal call shape is the minimum contract, which makes the
/// implicit `FormatWithLocation` conversion (a `consteval` constructor
/// defaulting its `std::source_location` to `current()`) part of the concept: a
/// candidate demanding an explicit argument pack or an explicit location does
/// not conform, while a conforming record carries the caller's
/// file/line/function — never a library-internal frame.
///
/// @tparam T Type under test; the concept constrains it, no external constraint
///         is imposed.
/// @note Spec `logging/logger-concept`, requirement "Logger concept core
///       surface".
/// @note Spec `logging/logger-concept`, requirement "Logger concept convenience
///       methods".
/// @note Spec `logging/logger-concept`, requirement "Convenience methods carry
///       call-site location".
/// @note Spec `logging/logger-concept`, requirement "Drop reporting moves to
///       snapshots".
template <class T>
concept Logger = requires(T t, const T ct, core::LogLevel lv) {
    /// @brief Runtime threshold control; relaxed cell, safe from any thread.
    { t.set_level(lv) } noexcept -> std::same_as<void>;
    /// @brief Read the active threshold (a relaxed snapshot, not sync).
    { ct.level() } noexcept -> std::same_as<core::LogLevel>;

    /// @brief Explicit-level entry point: filter, pack, enqueue; never blocks,
    ///        never returns a per-call outcome. The empty pack is the minimum
    ///        contract; argument packs are exercised by every sink test.
    { t.log(lv, "explicit") } noexcept -> std::same_as<void>;
    { t.log(lv, "explicit {}", 1) } noexcept -> std::same_as<void>;

    /// @brief Severity conveniences with implicit `FormatWithLocation` capture
    ///        (the message-only call shape is the requirement).
    { t.verbose("v") } noexcept -> std::same_as<void>;
    { t.debug("d") } noexcept -> std::same_as<void>;
    { t.info("i") } noexcept -> std::same_as<void>;
    { t.warning("w") } noexcept -> std::same_as<void>;
    { t.error("e") } noexcept -> std::same_as<void>;
    { t.fatal("f") } noexcept -> std::same_as<void>;
    /// @brief One convenience checked with an argument pack (spec "Argument
    ///        packs are accepted"); all six accept the same variadic shape.
    { t.info("i {}", 1) } noexcept -> std::same_as<void>;

    /// @brief Control-plane drain barrier; never a hot-path call, and outside
    ///        the never-block guarantee that binds the record members.
    { t.flush() } noexcept -> std::same_as<nexigen::core::Status>;
};

} // namespace nexigen::logging::concepts
