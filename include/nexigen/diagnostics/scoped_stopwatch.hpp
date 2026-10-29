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
// Timestamp::now() error; a Stamp_source::sample() guard has no failure branch
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

namespace nexigen::diagnostics {

namespace detail {

/// The tiniest destination: writes the delivered elapsed count into a
/// borrowed caller slot. This is what the std::uint64_t& constructor flavors
/// bind through CTAD (design D7).
struct OutRefSink {
    std::uint64_t* out; ///< Borrowed destination slot (never null in practice).

    constexpr explicit OutRefSink(std::uint64_t* slot) noexcept : out{slot} {}

    void operator()(std::uint64_t elapsed_ns) const noexcept {
        if (out != nullptr) { *out = elapsed_ns; }
    }
};

} // namespace detail

/// RAII scope measurement: samples at construction, delivers the scope's
/// elapsed nanoseconds exactly once at destruction (design D7; spec "RAII
/// scope measurement"). Non-copyable and non-movable — it owns exactly one
/// destination and one measurement, and there is no legal handoff. The guard
/// is an ephemeral stack object: deliberately NO alignas(64) (design D7 — the
/// 64-byte isolation guarantee belongs to the long-lived Stopwatch).
template <class Sink>
class ScopedStopwatch {
public:
    /// (1) Out-ref destination, default clock, no default budget.
    explicit ScopedStopwatch(std::uint64_t& out_ns) noexcept
        : anchor_ns_{anchor_for_(nullptr)},
          source_{nullptr},
          default_budget_{0U},
          sink_{detail::OutRefSink{&out_ns}} {}

    /// (2) Out-ref destination + trailing default budget (0 = none).
    ScopedStopwatch(std::uint64_t& out_ns, std::uint64_t budget_ns) noexcept
        : anchor_ns_{anchor_for_(nullptr)},
          source_{nullptr},
          default_budget_{budget_ns},
          sink_{detail::OutRefSink{&out_ns}} {}

    /// (3) Calibrated source + out-ref destination, optional default budget.
    ScopedStopwatch(chrono::Stamp_source* source, std::uint64_t& out_ns,
                    std::uint64_t budget_ns = 0U) noexcept
        : anchor_ns_{anchor_for_(source)},
          source_{source},
          default_budget_{budget_ns},
          sink_{detail::OutRefSink{&out_ns}} {}

    /// (4) Callable sink destination, default clock, no default budget. The
    /// integral static_assert keeps `ScopedStopwatch{out, budget}` from ever
    /// binding an integral to the sink slot (design D7 disambiguation).
    template <class S = Sink>
    explicit ScopedStopwatch(S&& sink) noexcept(
        std::is_nothrow_constructible_v<Sink, S&&>)
        : anchor_ns_{anchor_for_(nullptr)},
          source_{nullptr},
          default_budget_{0U},
          sink_{std::forward<S>(sink)} {
        static_assert(!std::is_integral_v<std::remove_cvref_t<S>>,
                      "an integral destination binds to the std::uint64_t& "
                      "constructor, never to the sink slot");
        static_assert(std::is_invocable_v<Sink&, std::uint64_t>,
                      "a ScopedStopwatch sink must be invocable with "
                      "std::uint64_t (the scope's elapsed nanoseconds)");
    }

    /// (5) Callable sink + trailing default budget (0 = none).
    template <class S = Sink>
    ScopedStopwatch(S&& sink, std::uint64_t budget_ns) noexcept(
        std::is_nothrow_constructible_v<Sink, S&&>)
        : anchor_ns_{anchor_for_(nullptr)},
          source_{nullptr},
          default_budget_{budget_ns},
          sink_{std::forward<S>(sink)} {
        static_assert(!std::is_integral_v<std::remove_cvref_t<S>>,
                      "an integral destination binds to the std::uint64_t& "
                      "constructor, never to the sink slot");
        static_assert(std::is_invocable_v<Sink&, std::uint64_t>,
                      "a ScopedStopwatch sink must be invocable with "
                      "std::uint64_t (the scope's elapsed nanoseconds)");
    }

    /// (6) Calibrated source + callable sink, optional default budget.
    template <class S = Sink>
    ScopedStopwatch(chrono::Stamp_source* source, S&& sink,
                    std::uint64_t budget_ns = 0U) noexcept(
        std::is_nothrow_constructible_v<Sink, S&&>)
        : anchor_ns_{anchor_for_(source)},
          source_{source},
          default_budget_{budget_ns},
          sink_{std::forward<S>(sink)} {
        static_assert(!std::is_integral_v<std::remove_cvref_t<S>>,
                      "an integral destination binds to the std::uint64_t& "
                      "constructor, never to the sink slot");
        static_assert(std::is_invocable_v<Sink&, std::uint64_t>,
                      "a ScopedStopwatch sink must be invocable with "
                      "std::uint64_t (the scope's elapsed nanoseconds)");
    }

    /// Non-copyable, non-movable (spec "Guard is not transferable"): the
    /// guard owns exactly one destination and one measurement.
    ScopedStopwatch(const ScopedStopwatch&) = delete;
    ScopedStopwatch& operator=(const ScopedStopwatch&) = delete;
    ScopedStopwatch(ScopedStopwatch&&) = delete;
    ScopedStopwatch& operator=(ScopedStopwatch&&) = delete;

    /// Exactly-once delivery (spec "Sink destination is invoked exactly
    /// once"): one sample, one write/invoke; uncommitted on a failed sample
    /// (design D3 policy — never fabricate a measurement).
    ~ScopedStopwatch() noexcept {
        if (reported_ || anchor_ns_ == 0U) { return; }
        std::uint64_t stamp = 0U;
        if (!sample_ok_(stamp)) { return; } // uncommitted: deliver nothing
        reported_ = true;
        sink_(stamp - anchor_ns_);
    }

    // -- Budget checks (design D8; spec "Budget-overrun check")

    /// Mid-scope overrun check against an explicit threshold: strictly
    /// greater ("exceeded"), one sample, const — never mutates the guard and
    /// never reports to the destination early.
    [[nodiscard]] bool has_exceeded_budget(std::uint64_t budget_ns) const noexcept {
        if (anchor_ns_ == 0U) { return false; }
        std::uint64_t stamp = 0U;
        if (!sample_ok_(stamp)) { return false; }
        return (stamp - anchor_ns_) > budget_ns;
    }

    /// Mid-scope overrun check against the stored default budget; false when
    /// none was configured (budget 0 = none).
    [[nodiscard]] bool has_exceeded_budget() const noexcept {
        return default_budget_ != 0U && has_exceeded_budget(default_budget_);
    }

    /// The stored default budget (0 = none configured).
    [[nodiscard]] constexpr std::uint64_t default_budget() const noexcept {
        return default_budget_;
    }

    /// The injected stamp source (nullptr = the default clock domain).
    [[nodiscard]] constexpr chrono::Stamp_source* source() const noexcept {
        return source_;
    }

    /// Cold-path diagnostic (mandate 9; design D9): active status, the
    /// elapsed-so-far and the pending default budget. The sink type never
    /// participates in rendering.
    [[nodiscard]] std::string to_string() const {
        std::uint64_t elapsed = 0U;
        if (anchor_ns_ != 0U) {
            std::uint64_t stamp = 0U;
            if (sample_ok_(stamp)) { elapsed = stamp - anchor_ns_; }
        }
        return "ScopedStopwatch(active=" +
               std::string(anchor_ns_ != 0U ? "true" : "false") +
               ", elapsed_ns=" + std::to_string(elapsed) +
               ", budget_ns=" + std::to_string(default_budget_) + ")";
    }

private:
    /// The start sample for a fresh guard; 0 = uncommitted (design D3 — a
    /// genuine monotonic stamp is never 0, so 0 is the failure witness).
    [[nodiscard]] static std::uint64_t anchor_for_(
        chrono::Stamp_source* source) noexcept {
        if (source != nullptr) { return source->sample().nanoseconds(); }
        const core::Result<chrono::Timestamp> stamp = chrono::Timestamp::now();
        return stamp ? stamp->nanoseconds() : 0U;
    }

    /// Result-free sampler face (same shape as Stopwatch's, design D3).
    [[nodiscard]] bool sample_ok_(std::uint64_t& stamp_ns_out) const noexcept {
        if (source_ != nullptr) {
            stamp_ns_out = source_->sample().nanoseconds();
            return true;
        }
        const core::Result<chrono::Timestamp> stamp = chrono::Timestamp::now();
        if (!stamp.has_value()) { return false; } // branch, never .value()
        stamp_ns_out = stamp->nanoseconds();
        return true;
    }

    /// [Design D7 member layout] anchor + source + budget + sink + flag:
    /// ~40–56 B with the common reference-capturing sink (N = 8). No
    /// alignas(64): ephemeral per-scope stack object, single-thread domain.
    std::uint64_t anchor_ns_;             ///< Start sample (0 = uncommitted).
    chrono::Stamp_source* source_;        ///< Borrowed sampler; null = default.
    std::uint64_t default_budget_;        ///< Stored budget (0 = none).
    Sink sink_;                           ///< Borrowed/stored destination.
    bool reported_{false};                ///< Belt for exactly-once delivery.
};

// --- CTAD guides (design D7): the out-ref flavors bind detail::OutRefSink and
// are non-template, so they win exact matches over the templated sink guides
// for `ScopedStopwatch{out}` / `ScopedStopwatch{out, budget}`.

ScopedStopwatch(std::uint64_t&) -> ScopedStopwatch<detail::OutRefSink>;
ScopedStopwatch(std::uint64_t&, std::uint64_t)
    -> ScopedStopwatch<detail::OutRefSink>;
ScopedStopwatch(chrono::Stamp_source*, std::uint64_t&)
    -> ScopedStopwatch<detail::OutRefSink>;
ScopedStopwatch(chrono::Stamp_source*, std::uint64_t&, std::uint64_t)
    -> ScopedStopwatch<detail::OutRefSink>;

// The sink flavors deduce the callable type (stored inline by value for
// rvalues, by reference for lvalues).
template <class Sink>
ScopedStopwatch(Sink&&) -> ScopedStopwatch<Sink>;
template <class Sink>
ScopedStopwatch(Sink&&, std::uint64_t) -> ScopedStopwatch<Sink>;
template <class Sink>
ScopedStopwatch(chrono::Stamp_source*, Sink&&) -> ScopedStopwatch<Sink>;
template <class Sink>
ScopedStopwatch(chrono::Stamp_source*, Sink&&, std::uint64_t)
    -> ScopedStopwatch<Sink>;

/// Free helper (mandate 9): mirrors the guard's member rendering.
template <class Sink>
[[nodiscard]] inline std::string to_string(const ScopedStopwatch<Sink>& guard) {
    return guard.to_string();
}

} // namespace nexigen::diagnostics

/// ScopedStopwatch formats as its rendered diagnostic text (design D9: the
/// partial specialization mirrors the tracing module's ScopedSpan shape).
template <class Sink>
struct std::formatter<nexigen::diagnostics::ScopedStopwatch<Sink>>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::diagnostics::ScopedStopwatch<Sink>& guard,
                Ctx& ctx) const {
        const std::string text = guard.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
