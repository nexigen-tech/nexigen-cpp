// nexigen::logging — variadic, virtual-free fan-out logger (design D16;
// format/multi-producer rework: change add-serialization-and-wait-free-
// logging, design D3/D10; spec logging/composite-logger).
//
// CompositeLogger<Loggers...> stores its members in a std::tuple and forwards
// every operation through C++23 fold expressions: no base class, no virtual
// table — each member call is statically resolved and inlined. The composite
// itself satisfies concepts::Logger (static_assert pinned at the bottom of
// this file), so composites nest freely. Variadic packs forward VERBATIM:
// the wrapper and argument pack are passed to every member, each member
// packs into its own shards (spec: "Every sink receives every accepted
// record").
//
// Level policy (spec "Level control propagation"):
//   - set_level() stores the composite's own threshold AND folds it through
//     every member (one call re-thresholds the whole tree).
//   - log()/conveniences short-circuit on the composite's OWN level (a
//     sub-threshold call never reaches any member), then fan out to every
//     member — each member re-applies its own filter, preserving per-sink
//     policies configured through get<I>()/get<T>().
//   - level() reports the composite's stored threshold.
//
// Member access: get<Index>() / get<Type>() return references for runtime
// reconfiguration. Type form requires the type to appear exactly once;
// out-of-range indices and absent/duplicated types fail at compile time via
// static_assert (spec "Invalid access rejected at compile time").
//
// Empty pack (CompositeLogger<>) is a valid silent logger: every record call
// returns normally and emits nothing (spec "Empty composite is valid").
//
// MULTI-PRODUCER CONTRACT (spec logging/multi-producer): any number of
// threads (bounded by each member's MaxThreads) may call log()/conveniences
// on the composite — fan-out preserves each async member's per-thread shard
// contract (and member rings are non-movable, so the composite is too).
// level() and every get<>()-accessor read relaxed SNAPSHOTS (diagnostics and
// configuration, not synchronization); flush() is the control-plane barrier
// across all members, outside the never-block guarantee of record methods.
//
// DROP SEMANTICS (record methods return void): each member counts its own
// drops in its own dropped() snapshot (reachable via get<>()); the composite
// adds no per-call reporting surface of its own — the old last-failure
// fold disappears together with the Status return type.
#pragma once

#include <cstddef>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include <nexigen/core/result.hpp>
#include <nexigen/logging/concepts/logger_concept.hpp>
#include <nexigen/logging/detail/format_param.hpp>
#include <nexigen/logging/detail/level_control.hpp>
#include <nexigen/core/log_level.hpp>

namespace nexigen::logging {

/// @brief Variadic, virtual-free fan-out over heterogeneous conforming loggers.
///
/// Stores its members in a `std::tuple` and forwards every operation through
/// C++23 fold expressions in pack order — no base class, no virtual table, so
/// each member call is statically resolved and inlinable. The composite itself
/// satisfies `concepts::Logger`, so composites nest freely and fan-out reaches
/// every leaf. Variadic packs are forwarded verbatim: the wrapper and the
/// argument pack reach every member, which then packs into its own shards.
///
/// `set_level()` stores the composite's own threshold and folds it through every
/// member (one call re-thresholds the whole tree); record calls short-circuit on
/// the composite's own level and then fan out, each member re-applying its own
/// filter. `flush()` drains all members and surfaces the first failure.
///
/// Any number of threads (bounded by each member's `MaxThreads`) may call the
/// record methods; `level()` reads the composite's relaxed cell and the
/// `get<>()` accessors hand out references for reconfiguration.
/// Non-copyable and non-movable because member rings are address-pinned.
///
/// @tparam Loggers Sinks satisfying `concepts::Logger`; the pack may be empty,
///         in which case every record call returns normally and emits nothing.
/// @note Spec `logging/composite-logger`, requirement "Virtual-free fan-out
///       composition".
/// @note Spec `logging/composite-logger`, requirement "Level control propagation".
/// @note Spec `logging/multi-producer`, requirement "Pre-allocated per-thread
///       shard array".
template <concepts::Logger... Loggers> class CompositeLogger final {
    static_assert((std::is_default_constructible_v<Loggers> && ...),
                  "composite members must be default-constructible (tuple-built)");

    /// @brief Compile-time index of the first `T` in the member pack.
    ///
    /// @tparam T Member type to locate.
    /// @return Index of the first match, or `sizeof...(Loggers)` as the
    ///         "not present" sentinel.
    /// @note The fold expands left to right and keeps the first hit.
    template <class T> [[nodiscard]] static constexpr std::size_t find_index() noexcept {
        std::size_t found = sizeof...(Loggers); // sentinel: "not present"
        std::size_t i = 0;
        ((std::is_same_v<T, Loggers> && found == sizeof...(Loggers) ? (found = i) : found, ++i),
         ...);
        return found;
    }

    /// @brief Test whether `T` appears exactly once in the member pack.
    ///
    /// @tparam T Member type to count.
    /// @return `true` when exactly one member has type `T`; the type-based
    ///         `get<T>()` requires it.
    /// @note Spec `logging/composite-logger`, requirement "Sub-logger access by
    ///       index and by type".
    template <class T> [[nodiscard]] static constexpr bool exactly_one() noexcept {
        std::size_t hits = 0;
        ((std::is_same_v<T, Loggers> ? ++hits : hits), ...);
        return hits == 1U;
    }

    /// @brief Apply `fn` to every stored logger, in pack order.
    ///
    /// The comma fold evaluates the members left to right, so a member's side
    /// effects (level changes, enqueues) are ordered by position in the pack. The
    /// `static_cast<void>` keeps the capture "used" for the empty pack, where a
    /// zero-expansion fold never mentions the operand.
    ///
    /// @tparam Fn Callable taking `Logger&`; return value discarded.
    /// @param fn Operation applied to each member once.
    template <typename Fn> void for_each(Fn&& fn) noexcept {
        std::apply(
            [&fn](Loggers&... lg) {
                static_cast<void>(fn);
                (fn(lg), ...);
            },
            members_);
    }

    /// @brief Apply `fn` to every stored logger through a const view.
    ///
    /// @tparam Fn Callable taking `const Logger&`; return value discarded.
    /// @param fn Operation applied to each member once, in pack order.
    template <typename Fn> void for_each(Fn&& fn) const noexcept {
        std::apply(
            [&fn](const Loggers&... lg) {
                static_cast<void>(fn);
                (fn(lg), ...);
            },
            members_);
    }

    /// @brief Stored members, in declaration order (index == pack position).
    std::tuple<Loggers...> members_{};
    /// @brief The composite's own relaxed threshold cell.
    detail::LevelControl level_{};

public:
    /// @brief Number of stored members (`sizeof...(Loggers)`).
    static constexpr std::size_t size = sizeof...(Loggers);

    /// @brief Construct every member default-constructed; `initialize()` them
    ///        individually through `get<>()` before logging.
    CompositeLogger() noexcept = default;
    /// @brief Copy construction is deleted; member rings are address-pinned.
    CompositeLogger(const CompositeLogger&) = delete;
    /// @brief Copy assignment is deleted; member rings are address-pinned.
    CompositeLogger& operator=(const CompositeLogger&) = delete;
    /// @brief Move construction is deleted; member rings are address-pinned.
    CompositeLogger(CompositeLogger&&) = delete;
    /// @brief Move assignment is deleted; member rings are address-pinned.
    CompositeLogger& operator=(CompositeLogger&&) = delete;

    /// @brief Access a sub-logger by index; duplicates are allowed.
    ///
    /// @tparam Index Position in the member pack; must be `< sizeof...(Loggers)`.
    /// @return Reference to the stored member, so its configuration can be changed
    ///         after construction.
    /// @note Spec `logging/composite-logger`, requirement "Sub-logger access by
    ///       index and by type": an out-of-range index is a compile-time error.
    template <std::size_t Index> [[nodiscard]] decltype(auto) get() noexcept {
        static_assert(Index < sizeof...(Loggers), "index beyond the member pack");
        return std::get<Index>(members_);
    }
    /// @brief Access a sub-logger by index through a const view.
    ///
    /// @tparam Index Position in the member pack; must be `< sizeof...(Loggers)`.
    /// @return Const reference to the stored member.
    template <std::size_t Index> [[nodiscard]] decltype(auto) get() const noexcept {
        static_assert(Index < sizeof...(Loggers), "index beyond the member pack");
        return std::get<Index>(members_);
    }

    /// @brief Access the unique sub-logger of type `T`.
    ///
    /// @tparam T Member type; must appear exactly once in the pack.
    /// @return Reference to the stored member, usable for per-sink
    ///         reconfiguration.
    /// @note A type that is absent or duplicated is a compile-time error — use
    ///       `get<Index>()` for duplicates.
    template <class T> [[nodiscard]] T& get() noexcept {
        static_assert(exactly_one<T>(), "type absent or duplicated — get<Index>() for duplicates");
        return std::get<find_index<T>()>(members_);
    }
    /// @brief Access the unique sub-logger of type `T` through a const view.
    ///
    /// @tparam T Member type; must appear exactly once in the pack.
    /// @return Const reference to the stored member.
    template <class T> [[nodiscard]] const T& get() const noexcept {
        static_assert(exactly_one<T>(), "type absent or duplicated — get<Index>() for duplicates");
        return std::get<find_index<T>()>(members_);
    }

    /// @brief Store the composite threshold and propagate it to every member.
    ///
    /// One call re-thresholds the whole tree: the composite's own cell is written
    /// first, then every member is visited in pack order. Per-member filters still
    /// apply per sink afterwards, so a member raised through `get<>()` keeps its
    /// stricter policy.
    ///
    /// @param new_level Threshold installed on the composite and on every member.
    /// @note Spec `logging/composite-logger`, requirement "Level control
    ///       propagation", scenario "One call moves the whole tree".
    void set_level(core::LogLevel new_level) noexcept {
        level_.set_level(new_level);
        for_each([new_level](auto& lg) noexcept { lg.set_level(new_level); });
    }
    /// @brief Read the composite's own stored threshold.
    ///
    /// @return The level installed by the last `set_level()` on this composite;
    ///         members may hold different thresholds.
    [[nodiscard]] core::LogLevel level() const noexcept { return level_.level(); }

    /// @brief Short-circuit on the composite level, then fan out statically.
    ///
    /// A call at or below `Off`, or below the composite's active level, returns
    /// without touching any member (bounded, no ring traffic). Otherwise every
    /// member is invoked exactly once, in pack order, with the same wrapper and
    /// the same forwarded argument pack.
    ///
    /// @tparam Args Argument types deduced from the trailing values; the same pack
    ///         reaches each member.
    /// @param record_level Severity carried by the record.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values forwarded unchanged to each member in declaration order.
    /// @note Spec `logging/composite-logger`, requirement "Virtual-free fan-out
    ///       composition": every sink receives every accepted record.
    /// @note Spec `logging/logger-concept`, requirement "Drop reporting moves to
    ///       snapshots": record methods return `void`.
    template <class... Args>
    void log(core::LogLevel record_level, detail::format_param_t<Args...> fmt,
             Args&&... args) noexcept {
        const core::LogLevel active = level_.level();
        if (active == core::LogLevel::Off || record_level < active) {
            return; // bounded short-circuit: folds never run (spec)
        }
        for_each(
            [&](auto& lg) noexcept { lg.log(record_level, fmt, std::forward<Args>(args)...); });
    }

    /// @brief Fan out a `Verbose` record to every member.
    ///
    /// @tparam Args Argument types deduced from the trailing values.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values forwarded unchanged to each member.
    template <class... Args>
    void verbose(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(core::LogLevel::Verbose, fmt, std::forward<Args>(args)...);
    }
    /// @brief Fan out a `Debug` record to every member.
    ///
    /// @tparam Args Argument types deduced from the trailing values.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values forwarded unchanged to each member.
    template <class... Args>
    void debug(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(core::LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }
    /// @brief Fan out an `Info` record to every member.
    ///
    /// @tparam Args Argument types deduced from the trailing values.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values forwarded unchanged to each member.
    template <class... Args>
    void info(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(core::LogLevel::Info, fmt, std::forward<Args>(args)...);
    }
    /// @brief Fan out a `Warning` record to every member.
    ///
    /// @tparam Args Argument types deduced from the trailing values.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values forwarded unchanged to each member.
    template <class... Args>
    void warning(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(core::LogLevel::Warning, fmt, std::forward<Args>(args)...);
    }
    /// @brief Fan out an `Error` record to every member.
    ///
    /// @tparam Args Argument types deduced from the trailing values.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values forwarded unchanged to each member.
    template <class... Args>
    void error(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(core::LogLevel::Error, fmt, std::forward<Args>(args)...);
    }
    /// @brief Fan out a `Fatal` record to every member.
    ///
    /// @tparam Args Argument types deduced from the trailing values.
    /// @param fmt Checked format literal plus the caller's source location.
    /// @param args Values forwarded unchanged to each member.
    template <class... Args>
    void fatal(detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(core::LogLevel::Fatal, fmt, std::forward<Args>(args)...);
    }

    /// @brief Control-plane barrier across every member.
    ///
    /// Every member is flushed (no short-circuiting) and the FIRST failure wins:
    /// an OK from a later member never masks an earlier rejection. With an empty
    /// pack the result is OK.
    ///
    /// @return OK when every member drained cleanly, otherwise the status of the
    ///         first failing member. Never inspect it via `.value()`.
    /// @warning Not a hot-path call: it may block in each member's barrier.
    /// @note Spec `logging/composite-logger`, requirement "Virtual-free fan-out
    ///       composition".
    [[nodiscard]] core::Status flush() noexcept {
        // First error wins (parity with CompositeTraceSink — analysis #21):
        // every member is still flushed (no short-circuiting); later
        // members' errors are skipped in favor of the first one.
        core::Status first{};
        bool have_first = false;
        for_each([&](auto& lg) noexcept {
            const core::Status s = lg.flush();
            if (!s && !have_first) {
                first = s;
                have_first = true;
            }
        });
        return first;
    }

    /// @brief Render the whole tree for cold-path diagnostics.
    ///
    /// @return The composite's own level and member count, followed by each
    ///         member's `to_string()` in pack order (nested composites recurse).
    /// @note Mandate 9 (`to_string`), and spec `logging/composite-logger`,
    ///       requirement "Composite logger diagnostic string"; only snapshot
    ///       queries are touched, never hot-path state.
    [[nodiscard]] std::string to_string() const {
        std::string out = "CompositeLogger(level=" + std::string(core::to_string(level_.level())) +
                          ", members=" + std::to_string(sizeof...(Loggers));
        std::apply(
            [&](const Loggers&... lg) {
                static_cast<void>(out);
                // Member call: public to_string() const is part of the
                // logger-type contract (namespaces of member types need not
                // be visible at this header's point of definition).
                ((out += ", [" + lg.to_string() + "]"), ...);
            },
            members_);
        out += ")";
        return out;
    }
};

/// @brief Free helper mirroring `CompositeLogger::to_string`.
///
/// @tparam Loggers Member pack of the composite being dumped.
/// @param logger Instance to render; only snapshot queries are issued.
/// @return The same text as `logger.to_string()`.
/// @note Mandate 9 (`to_string`).
template <class... Loggers>
[[nodiscard]] std::string to_string(const CompositeLogger<Loggers...>& logger) {
    return logger.to_string();
}

static_assert(concepts::Logger<CompositeLogger<>>,
              "empty composite must satisfy the Logger contract (spec scenario)");

} // namespace nexigen::logging
