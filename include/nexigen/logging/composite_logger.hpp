// nexigen::logging — variadic, virtual-free fan-out logger (design.md D16,
// spec logging/composite-logger).
//
// CompositeLogger<Loggers...> stores its members in a std::tuple and forwards
// every operation through C++23 fold expressions: no base class, no virtual
// table — each member call is statically resolved and inlined. The composite
// itself satisfies concepts::Logger (static_assert pinned at the bottom of
// this file), so composites nest freely.
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
// Empty pack (CompositeLogger<>) is a valid silent logger: every call returns
// OK and emits nothing (spec "Empty composite is valid").
//
// SPSC note: one thread calling log() on the composite is the single producer
// for EACH async member — fan-out preserves the per-instance contract (and
// the members' rings are non-movable, so the composite is non-movable too).
// level() and every get<>()-accessor read relaxed SNAPSHOTS (diagnostics and
// configuration, not synchronization); flush() is the control-plane barrier
// across all members, outside the never-block guarantee of log()/conveniences.
//
// DROP SEMANTICS (returned Status of log()/conveniences):
//   {}              the composite short-circuited on its own level, OR every
//                   member accepted/filtered the record (members filter at
//                   their own thresholds)
//   BufferFull      some member's ring rejected the push (counted there)
//   RateLimited     some member's throttle spent its window (counted there)
// A rejected record is surfaced as the LAST failure across the member fold.
#pragma once

#include <cstddef>
#include <source_location>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <nexigen/core/result.hpp>
#include <nexigen/logging/concepts/logger_concept.hpp>
#include <nexigen/logging/detail/level_control.hpp>
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
    /// Every member is invoked exactly once with the same record; if any sink
    /// rejected the record its status is surfaced — the LAST failure wins (a
    /// later OK never masks it). Drop accounting per sink remains available
    /// on the members themselves.
    [[nodiscard]] core::Status log(LogLevel record_level, std::source_location loc,
                                   std::string_view message) noexcept {
        const LogLevel active = level_.level();
        if (active == LogLevel::Off || record_level < active) {
            return {}; // bounded short-circuit: folds never run (spec)
        }
        core::Status last{};
        for_each([&](auto& lg) noexcept {
            const core::Status s = lg.log(record_level, loc, message);
            if (!s) {
                last = s;
            }
        });
        return last;
    }

    [[nodiscard]] core::Status verbose(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept {
        return log(LogLevel::Verbose, loc, msg);
    }
    [[nodiscard]] core::Status debug(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept {
        return log(LogLevel::Debug, loc, msg);
    }
    [[nodiscard]] core::Status info(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept {
        return log(LogLevel::Info, loc, msg);
    }
    [[nodiscard]] core::Status warning(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept {
        return log(LogLevel::Warning, loc, msg);
    }
    [[nodiscard]] core::Status error(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept {
        return log(LogLevel::Error, loc, msg);
    }
    [[nodiscard]] core::Status fatal(std::string_view msg, std::source_location loc = std::source_location::current()) noexcept {
        return log(LogLevel::Fatal, loc, msg);
    }

    /// Control-plane barrier across all members; the last failure surfaced
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
};

static_assert(concepts::Logger<CompositeLogger<>>,
              "empty composite must satisfy the Logger contract (spec scenario)");

} // namespace nexigen::logging
