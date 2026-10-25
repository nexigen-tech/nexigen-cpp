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
#include <nexigen/logging/detail/level_control.hpp>
#include <nexigen/logging/format_with_location.hpp>
#include <nexigen/logging/log_level.hpp>

namespace nexigen::logging {

/// Fan-out over heterogeneous conforming loggers, zero virtual dispatch.
template <concepts::Logger... Loggers>
class CompositeLogger final {
    static_assert((std::is_default_constructible_v<Loggers> && ...),
                  "composite members must be default-constructible (tuple-built)");

    template <class T>
    [[nodiscard]] static constexpr std::size_t find_index() noexcept {
        std::size_t found = sizeof...(Loggers); // sentinel: "not present"
        std::size_t i = 0;
        ((std::is_same_v<T, Loggers> && found == sizeof...(Loggers) ? (found = i) : found,
          ++i),
         ...);
        return found;
    }

    template <class T>
    [[nodiscard]] static constexpr bool exactly_one() noexcept {
        std::size_t hits = 0;
        ((std::is_same_v<T, Loggers> ? ++hits : hits), ...);
        return hits == 1U;
    }

    /// Fold helper: apply fn(member) to every stored logger, in pack order.
    /// The static_cast<void> keeps the capture "used" for the empty pack
    /// (Clang -Wunused-lambda-capture: a zero-expansion comma fold never
    /// mentions the operand).
    template <typename Fn>
    void for_each(Fn&& fn) noexcept {
        std::apply([&fn](Loggers&... lg) {
            static_cast<void>(fn);
            (fn(lg), ...);
        }, members_);
    }

    template <typename Fn>
    void for_each(Fn&& fn) const noexcept {
        std::apply([&fn](const Loggers&... lg) {
            static_cast<void>(fn);
            (fn(lg), ...);
        }, members_);
    }

    std::tuple<Loggers...> members_{};
    detail::Level_control level_{};

public:
    static constexpr std::size_t size = sizeof...(Loggers);

    CompositeLogger() noexcept = default;
    CompositeLogger(const CompositeLogger&) = delete;
    CompositeLogger& operator=(const CompositeLogger&) = delete;
    CompositeLogger(CompositeLogger&&) = delete;
    CompositeLogger& operator=(CompositeLogger&&) = delete;

    /// Sub-logger access by index (duplicates allowed).
    template <std::size_t Index>
    [[nodiscard]] decltype(auto) get() noexcept {
        static_assert(Index < sizeof...(Loggers), "index beyond the member pack");
        return std::get<Index>(members_);
    }
    template <std::size_t Index>
    [[nodiscard]] decltype(auto) get() const noexcept {
        static_assert(Index < sizeof...(Loggers), "index beyond the member pack");
        return std::get<Index>(members_);
    }

    /// Sub-logger access by type (must be unique in the pack).
    template <class T>
    [[nodiscard]] T& get() noexcept {
        static_assert(exactly_one<T>(),
                      "type absent or duplicated — get<Index>() for duplicates");
        return std::get<find_index<T>()>(members_);
    }
    template <class T>
    [[nodiscard]] const T& get() const noexcept {
        static_assert(exactly_one<T>(),
                      "type absent or duplicated — get<Index>() for duplicates");
        return std::get<find_index<T>()>(members_);
    }

    /// Store own threshold and fold it through every member (spec: one call
    /// moves the whole tree; per-member filters still apply per sink).
    void set_level(LogLevel new_level) noexcept {
        level_.set_level(new_level);
        for_each([new_level](auto& lg) noexcept { lg.set_level(new_level); });
    }
    [[nodiscard]] LogLevel level() const noexcept { return level_.level(); }

    /// Composite short-circuit on the OWN threshold, then static fan-out.
    /// Every member is invoked exactly once with the same wrapper and pack;
    /// record methods return void (spec logging/logger-concept).
    template <class... Args>
    void log(LogLevel record_level, detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        const LogLevel active = level_.level();
        if (active == LogLevel::Off || record_level < active) {
            return; // bounded short-circuit: folds never run (spec)
        }
        for_each([&](auto& lg) noexcept {
            lg.log(record_level, fmt, std::forward<Args>(args)...);
        });
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

    /// Control-plane barrier across all members; the last failure surfaces
    /// (an OK from a later member never masks an earlier rejection).
    [[nodiscard]] core::Status flush() noexcept {
        core::Status last{};
        for_each([&](auto& lg) noexcept {
            const core::Status s = lg.flush();
            if (!s) {
                last = s;
            }
        });
        return last;
    }

    /// Cold-path diagnostic (mandate 9): own level + member count, embedding
    /// each member's own to_string() via the fold (nested composites recurse).
    [[nodiscard]] std::string to_string() const {
        std::string out = "CompositeLogger(level=" +
                          std::string(logging::to_string(level_.level())) +
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

/// Free helper (mandate 9): mirrors the member.
template <class... Loggers>
[[nodiscard]] std::string to_string(const CompositeLogger<Loggers...>& logger) {
    return logger.to_string();
}

static_assert(concepts::Logger<CompositeLogger<>>,
              "empty composite must satisfy the Logger contract (spec scenario)");

} // namespace nexigen::logging
