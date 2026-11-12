// nexigen::diagnostics — the RAII scope guard (design.md D7–D9; spec
// diagnostics/scoped-stopwatch).
//
// ScopedStopwatch measures one scope: it samples the start timestamp at
// construction and, exactly once at destruction, computes the scope's elapsed
// nanoseconds and delivers it to a caller-provided destination — either a
// std::uint64_t& or a non-throwing callable sink invoked with the count. One
// class template covers both destination kinds (design D7): the out-ref flavor
// binds Sink = detail::OutRefSink through deduction guides, the sink flavor
// stores the callable inline by value (rvalues) or by reference (lvalues) —
// zero allocation, zero indirection, no type-erased std::function anywhere.
//
// HOT PATH (design D7 accounting): construction = one sample + fixed stores;
// destruction = one sample + one delivery; has_exceeded_budget = one sample.
// No heap, no locks, no atomics, no formatting — the whole surface is noexcept
// (the reporting call cannot throw under the build's -fno-exceptions; no
// is_nothrow_invocable gate is imposed on sinks, per design D7).
//
// Clock-failure policy: the design D3 uncommitted rule carries over — a failed
// construction sample leaves the guard unanchored and a failed destruction or
// check sample delivers/reports nothing rather than fabricating a measurement.
// The default-clock path can only fail through the practically-impossible
// Timestamp::now() error; a StampSource::sample() guard has no failure branch
// at all on x86-64.
//
// Diagnostics (design D9; mandate 9): inline member + free to_string() and a
// partial std::formatter<ScopedStopwatch<Sink>> — cold-path only, the sink
// type never participates in rendering. A live guard is not restorable state:
// no from_json (mirrors the tracing module's ScopedSpan).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <type_traits>
#include <utility>

#include <nexigen/chrono/stamp_source.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/diagnostics/out_ref_sink.hpp>

namespace nexigen::diagnostics {

/// @brief RAII scope measurement delivering one elapsed value at scope exit.
///
/// Samples the start timestamp at construction and, exactly once at
/// destruction, computes the scope's elapsed nanoseconds and delivers it to the
/// destination bound at construction: a borrowed `std::uint64_t` slot or a
/// caller callable. Non-copyable and non-movable because it owns exactly one
/// destination and one measurement, with no legal handoff.
///
/// The guard is an ephemeral per-scope stack object in a single-thread domain:
/// deliberately no `alignas(64)` (the 64-byte isolation guarantee belongs to the
/// long-lived `Stopwatch`). Construction pays one sample plus fixed stores,
/// destruction one sample plus one delivery, and each budget query one sample —
/// no heap, no locks, no atomics, no formatting.
///
/// A failed construction sample leaves the guard unanchored, and a failed
/// destruction or check sample delivers/reports nothing instead of fabricating a
/// measurement.
///
/// @tparam Sink Destination type: `detail::OutRefSink` for the out-ref flavors or
///         the caller's callable, which must be invocable as `Sink&` with
///         `std::uint64_t` (enforced by `static_assert`, not a named concept).
/// @note Spec `diagnostics/scoped-stopwatch`, requirement "RAII scope
///       measurement".
/// @note Spec `diagnostics/scoped-stopwatch`, requirement "Hot-path and layout
///       guarantees".
/// @note Diagnostic rendering follows project mandates 9 and 11 and never runs
///       on the guard hot path; the sink type does not participate in rendering.
/// @warning The destination (slot or callable) is borrowed and must outlive the
///          guard; the type is confined to one thread.
template <class Sink> class ScopedStopwatch {
public:
    /// @brief Construct with a borrowed out-reference and the default clock.
    ///
    /// @param out_ns Destination slot written once with the scope's elapsed
    ///        nanoseconds; must outlive the guard.
    /// @note Spec `diagnostics/scoped-stopwatch`, requirement "RAII scope
    ///       measurement"; equivalent to a zero default budget.
    explicit ScopedStopwatch(std::uint64_t& out_ns) noexcept
        : anchor_ns_{anchor_for_(nullptr)}, source_{nullptr}, default_budget_{0U},
          sink_{detail::OutRefSink{&out_ns}} {}

    /// @brief Construct with a borrowed out-reference and a default budget.
    ///
    /// @param out_ns Destination slot written once at scope exit.
    /// @param budget_ns Default budget checked by the no-argument
    ///        `has_exceeded_budget()`; `0` means none.
    ScopedStopwatch(std::uint64_t& out_ns, std::uint64_t budget_ns) noexcept
        : anchor_ns_{anchor_for_(nullptr)}, source_{nullptr}, default_budget_{budget_ns},
          sink_{detail::OutRefSink{&out_ns}} {}

    /// @brief Construct with a calibrated source and a borrowed out-reference.
    ///
    /// @param source Borrowed sampler; null selects the default clock domain.
    /// @param out_ns Destination slot written once at scope exit.
    /// @param budget_ns Default budget; `0` means none.
    /// @note Spec `diagnostics/scoped-stopwatch`, requirement "Construction
    ///       overloads and default-clock fallback".
    ScopedStopwatch(chrono::StampSource* source, std::uint64_t& out_ns,
                    std::uint64_t budget_ns = 0U) noexcept
        : anchor_ns_{anchor_for_(source)}, source_{source}, default_budget_{budget_ns},
          sink_{detail::OutRefSink{&out_ns}} {}

    /// @brief Construct with a callable sink and the default clock.
    ///
    /// The integral `static_assert` keeps `ScopedStopwatch{out, budget}` from
    /// ever binding an integral to the sink slot.
    ///
    /// @tparam S Deduced destination argument, constrained by `static_assert` to
    ///         be non-integral and invocable as `Sink&` with `std::uint64_t`.
    /// @param sink Destination callable stored by value for rvalues and by
    ///        reference for lvalues; must outlive the guard.
    /// @note Spec `diagnostics/scoped-stopwatch`, scenario "Destination
    ///       construction is unambiguous".
    template <class S = Sink>
    explicit ScopedStopwatch(S&& sink) noexcept(std::is_nothrow_constructible_v<Sink, S&&>)
        : anchor_ns_{anchor_for_(nullptr)}, source_{nullptr}, default_budget_{0U},
          sink_{std::forward<S>(sink)} {
        static_assert(!std::is_integral_v<std::remove_cvref_t<S>>,
                      "an integral destination binds to the std::uint64_t& "
                      "constructor, never to the sink slot");
        static_assert(std::is_invocable_v<Sink&, std::uint64_t>,
                      "a ScopedStopwatch sink must be invocable with "
                      "std::uint64_t (the scope's elapsed nanoseconds)");
    }

    /// @brief Construct with a callable sink and a default budget.
    ///
    /// @tparam S Deduced destination argument, constrained by `static_assert` to
    ///         be non-integral and invocable as `Sink&` with `std::uint64_t`.
    /// @param sink Destination callable stored inline; must outlive the guard.
    /// @param budget_ns Default budget; `0` means none.
    template <class S = Sink>
    ScopedStopwatch(S&& sink,
                    std::uint64_t budget_ns) noexcept(std::is_nothrow_constructible_v<Sink, S&&>)
        : anchor_ns_{anchor_for_(nullptr)}, source_{nullptr}, default_budget_{budget_ns},
          sink_{std::forward<S>(sink)} {
        static_assert(!std::is_integral_v<std::remove_cvref_t<S>>,
                      "an integral destination binds to the std::uint64_t& "
                      "constructor, never to the sink slot");
        static_assert(std::is_invocable_v<Sink&, std::uint64_t>,
                      "a ScopedStopwatch sink must be invocable with "
                      "std::uint64_t (the scope's elapsed nanoseconds)");
    }

    /// @brief Construct with a calibrated source, a callable sink and a budget.
    ///
    /// @tparam S Deduced destination argument, constrained by `static_assert` to
    ///         be non-integral and invocable as `Sink&` with `std::uint64_t`.
    /// @param source Borrowed sampler; null selects the default clock domain.
    /// @param sink Destination callable stored inline; must outlive the guard.
    /// @param budget_ns Default budget; `0` means none.
    /// @note Spec `diagnostics/scoped-stopwatch`, scenario "Source and budget
    ///       combine with each destination".
    template <class S = Sink>
    ScopedStopwatch(chrono::StampSource* source, S&& sink, std::uint64_t budget_ns = 0U) noexcept(
        std::is_nothrow_constructible_v<Sink, S&&>)
        : anchor_ns_{anchor_for_(source)}, source_{source}, default_budget_{budget_ns},
          sink_{std::forward<S>(sink)} {
        static_assert(!std::is_integral_v<std::remove_cvref_t<S>>,
                      "an integral destination binds to the std::uint64_t& "
                      "constructor, never to the sink slot");
        static_assert(std::is_invocable_v<Sink&, std::uint64_t>,
                      "a ScopedStopwatch sink must be invocable with "
                      "std::uint64_t (the scope's elapsed nanoseconds)");
    }

    /// @brief Copying is impossible: the guard owns one destination and one measurement.
    ScopedStopwatch(const ScopedStopwatch&) = delete;
    /// @brief Copy assignment is impossible: the guard owns one destination.
    ScopedStopwatch& operator=(const ScopedStopwatch&) = delete;
    /// @brief Moving is impossible: there is no legal handoff of the measurement.
    ScopedStopwatch(ScopedStopwatch&&) = delete;
    /// @brief Move assignment is impossible: there is no legal handoff.
    ScopedStopwatch& operator=(ScopedStopwatch&&) = delete;

    /// @brief Deliver the scope's elapsed nanoseconds exactly once.
    ///
    /// Takes one sample and performs one write/invoke; the guard stays
    /// uncommitted when no anchor exists or the sample fails, so no fabricated
    /// measurement is ever delivered.
    ///
    /// @note Spec `diagnostics/scoped-stopwatch`, scenario "Sink destination is
    ///       invoked exactly once".
    /// @note `noexcept`: the reporting call cannot throw under the build's
    ///       `-fno-exceptions`, so no nothrow gate is imposed on sinks.
    ~ScopedStopwatch() noexcept {
        if (reported_ || anchor_ns_ == 0U) {
            return;
        }
        std::uint64_t stamp = 0U;
        if (!sample_ok_(stamp)) {
            return;
        } // uncommitted: deliver nothing
        reported_ = true;
        sink_(stamp - anchor_ns_);
    }

    // -- Budget checks (design D8; spec "Budget-overrun check")

    /// @brief Mid-scope overrun check against an explicit threshold.
    ///
    /// Reports strictly-greater elapsed-so-far ("exceeded") with one sample;
    /// `const`, so the guard is never mutated and nothing is delivered early.
    ///
    /// @param budget_ns Threshold in nanoseconds; equality is within budget.
    /// @return `true` when elapsed-so-far is strictly greater than
    ///         `budget_ns`; `false` when unanchored or the sample fails.
    /// @note Spec `diagnostics/scoped-stopwatch`, requirement "Budget-overrun
    ///       check".
    [[nodiscard]] bool has_exceeded_budget(std::uint64_t budget_ns) const noexcept {
        if (anchor_ns_ == 0U) {
            return false;
        }
        std::uint64_t stamp = 0U;
        if (!sample_ok_(stamp)) {
            return false;
        }
        return (stamp - anchor_ns_) > budget_ns;
    }

    /// @brief Mid-scope overrun check against the stored default budget.
    ///
    /// @return `false` when no default budget was configured (`0` means none);
    ///         otherwise the explicit-threshold result for that budget.
    /// @note Spec `diagnostics/scoped-stopwatch`, requirement "Budget-overrun
    ///       check".
    [[nodiscard]] bool has_exceeded_budget() const noexcept {
        return default_budget_ != 0U && has_exceeded_budget(default_budget_);
    }

    /// @brief The default budget stored at construction.
    ///
    /// @return Budget in nanoseconds; `0` means none was configured.
    [[nodiscard]] constexpr std::uint64_t default_budget() const noexcept {
        return default_budget_;
    }

    /// @brief The injected stamp source.
    ///
    /// @return The borrowed source, or `nullptr` for the default clock domain.
    [[nodiscard]] constexpr chrono::StampSource* source() const noexcept { return source_; }

    /// @brief Render the live guard as a cold-path diagnostic string.
    ///
    /// Reports the active status, the elapsed-so-far and the pending default
    /// budget; the sink type never participates in rendering.
    ///
    /// @return Diagnostic text; identical to the free `to_string` helper.
    /// @note Spec `diagnostics/scoped-stopwatch`, requirement "ScopedStopwatch
    ///       diagnostic rendering"; diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const {
        std::uint64_t elapsed = 0U;
        if (anchor_ns_ != 0U) {
            std::uint64_t stamp = 0U;
            if (sample_ok_(stamp)) {
                elapsed = stamp - anchor_ns_;
            }
        }
        return "ScopedStopwatch(active=" + std::string(anchor_ns_ != 0U ? "true" : "false") +
               ", elapsed_ns=" + std::to_string(elapsed) +
               ", budget_ns=" + std::to_string(default_budget_) + ")";
    }

private:
    /// @brief Take the start sample for a fresh guard.
    ///
    /// @param source Borrowed sampler, or null for the default clock.
    /// @return The raw nanosecond stamp, or `0` when uncommitted (a genuine
    ///         monotonic stamp is never `0`, so `0` is the failure witness).
    [[nodiscard]] static std::uint64_t anchor_for_(chrono::StampSource* source) noexcept {
        if (source != nullptr) {
            return source->sample().nanoseconds();
        }
        const core::Result<chrono::Timestamp> stamp = chrono::Timestamp::now();
        return stamp ? stamp->nanoseconds() : 0U;
    }

    /// @brief Result-free sampler face used by destruction and budget checks.
    ///
    /// @param stamp_ns_out Destination for the raw stamp on success.
    /// @return `true` with `stamp_ns_out` assigned on a good sample; `false` on
    ///         a default-clock failure (an injected source never fails).
    [[nodiscard]] bool sample_ok_(std::uint64_t& stamp_ns_out) const noexcept {
        if (source_ != nullptr) {
            stamp_ns_out = source_->sample().nanoseconds();
            return true;
        }
        const core::Result<chrono::Timestamp> stamp = chrono::Timestamp::now();
        if (!stamp.has_value()) {
            return false;
        } // branch, never .value()
        stamp_ns_out = stamp->nanoseconds();
        return true;
    }

    /// @brief Start sample; `0` marks the guard as uncommitted.
    ///
    /// Layout: anchor + source + budget + sink + flag is ~40–56 B with the common
    /// reference-capturing sink. No `alignas(64)`: ephemeral per-scope stack
    /// object in a single-thread domain.
    std::uint64_t anchor_ns_;      ///< Start sample (0 = uncommitted).
    chrono::StampSource* source_;  ///< Borrowed sampler; null = default.
    std::uint64_t default_budget_; ///< Stored budget (0 = none).
    Sink sink_;                    ///< Borrowed/stored destination.
    bool reported_{false};         ///< Belt for exactly-once delivery.
};

// --- CTAD guides (design D7): the out-ref flavors bind detail::OutRefSink and
// are non-template, so they win exact matches over the templated sink guides
// for `ScopedStopwatch{out}` / `ScopedStopwatch{out, budget}`.

/// @brief Deduce the out-ref flavor from a lone destination slot.
ScopedStopwatch(std::uint64_t&) -> ScopedStopwatch<detail::OutRefSink>;
/// @brief Deduce the out-ref flavor from a slot and a default budget.
ScopedStopwatch(std::uint64_t&, std::uint64_t) -> ScopedStopwatch<detail::OutRefSink>;
/// @brief Deduce the out-ref flavor from a source and a slot.
ScopedStopwatch(chrono::StampSource*, std::uint64_t&) -> ScopedStopwatch<detail::OutRefSink>;
/// @brief Deduce the out-ref flavor from a source, a slot and a budget.
ScopedStopwatch(chrono::StampSource*, std::uint64_t&, std::uint64_t)
    -> ScopedStopwatch<detail::OutRefSink>;

// The sink flavors deduce the callable type (stored inline by value for
// rvalues, by reference for lvalues).

/// @brief Deduce the sink flavor from a callable destination.
///
/// @tparam Sink Deduced callable type stored inline.
template <class Sink> ScopedStopwatch(Sink&&) -> ScopedStopwatch<Sink>;
/// @brief Deduce the sink flavor from a callable and a default budget.
///
/// @tparam Sink Deduced callable type stored inline.
template <class Sink> ScopedStopwatch(Sink&&, std::uint64_t) -> ScopedStopwatch<Sink>;
/// @brief Deduce the sink flavor from a source and a callable.
///
/// @tparam Sink Deduced callable type stored inline.
template <class Sink> ScopedStopwatch(chrono::StampSource*, Sink&&) -> ScopedStopwatch<Sink>;
/// @brief Deduce the sink flavor from a source, a callable and a budget.
///
/// @tparam Sink Deduced callable type stored inline.
template <class Sink>
ScopedStopwatch(chrono::StampSource*, Sink&&, std::uint64_t) -> ScopedStopwatch<Sink>;

/// @brief Free diagnostic helper mirroring `ScopedStopwatch::to_string()`.
///
/// @tparam Sink Destination type of the guard.
/// @param guard Live guard to render.
/// @return The same text the member returns.
/// @note Project mandate 9; cold path only (allocates a `std::string`).
template <class Sink>
[[nodiscard]] inline std::string to_string(const ScopedStopwatch<Sink>& guard) {
    return guard.to_string();
}

} // namespace nexigen::diagnostics

/// @brief `std::formatter` rendering a guard as its diagnostic text.
///
/// A partial specialization over the sink type; fill, width and alignment defer
/// to the `std::string_view` formatter. Rendering calls `to_string()`, so it is
/// cold path only and never runs during the guard lifecycle.
///
/// @tparam Sink Destination type of the guard.
/// @note Spec `diagnostics/scoped-stopwatch`, requirement "ScopedStopwatch
///       diagnostic rendering"; project mandate 11.
template <class Sink>
struct std::formatter<nexigen::diagnostics::ScopedStopwatch<Sink>>
    : std::formatter<std::string_view> {
    /// @brief Format the guard through its rendered diagnostic text.
    ///
    /// @tparam Ctx Format context supplied by `std::format`.
    /// @param guard Live guard to render.
    /// @param ctx Format context receiving the rendered text.
    /// @return The formatter's output iterator.
    template <class Ctx>
    auto format(const nexigen::diagnostics::ScopedStopwatch<Sink>& guard, Ctx& ctx) const {
        const std::string text = guard.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
