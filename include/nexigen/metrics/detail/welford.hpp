// nexigen::metrics — Welford moment accumulators (design D9).
//
// The online statistics update (Welford) keeps count/mean/M2 in O(1) per
// sample with no allocation, and shards merge with Chan's parallel merge
// formula, so a sharded instrument reports the same moments as a single
// threaded one.
//
// std is the POPULATION standard deviation: sqrt(M2 / count). It is 0 for a
// single sample (the sample deviation would be undefined) and 0 for an empty
// instrument (all summary fields zeroed).
#pragma once

#include <cmath>
#include <cstdint>

namespace nexigen::metrics::detail {

/// @brief Plain moment state (the observable summary shape).
///
/// Holds count/mean/M2 plus the running extremes. Kept as a trivially copyable
/// aggregate so a shard slot and a merged summary have the same layout; an empty
/// state has every field zeroed.
///
/// @note Spec `metrics/descriptive-statistics`, requirement "Online moment
///       statistics".
struct WelfordState {
    /// Number of samples folded in; `0` means empty.
    std::uint64_t count{0};
    /// Running mean of the samples.
    double mean{0.0};
    /// Sum of squared deviations from the mean (Welford's M2).
    double m2{0.0};
    /// Smallest observed sample; `0.0` when empty.
    double min{0.0};
    /// Largest observed sample; `0.0` when empty.
    double max{0.0};
};

/// @brief Population standard deviation of a moment state.
///
/// Computes `sqrt(m2 / count)`, the population (not sample) deviation: it is `0`
/// for an empty state and `0` for a single sample, where the sample deviation
/// would be undefined.
///
/// @param state Moment state to summarize.
/// @return Population standard deviation, or `0.0` when `count == 0`.
/// @note Spec `metrics/descriptive-statistics`, requirement "Online moment
///       statistics".
[[nodiscard]] inline double population_std(const WelfordState& state) noexcept {
    if (state.count == 0U) {
        return 0.0;
    }
    return std::sqrt(state.m2 / static_cast<double>(state.count));
}

/// @brief Fold one sample into a moment state (Welford's update).
///
/// O(1) per sample with no allocation and no locks; the first sample seeds
/// count/mean/min/max and leaves `m2` at zero. The caller owns the state.
///
/// @param state Moment state updated in place.
/// @param value Sample to fold in.
/// @note Spec `metrics/descriptive-statistics`, requirement "Online moment
///       statistics".
inline void welford_add(WelfordState& state, double value) noexcept {
    if (state.count == 0U) {
        state.count = 1U;
        state.mean = value;
        state.m2 = 0.0;
        state.min = value;
        state.max = value;
        return;
    }
    ++state.count;
    const double delta = value - state.mean;
    state.mean += delta / static_cast<double>(state.count);
    const double delta2 = value - state.mean;
    state.m2 += delta * delta2;
    if (value < state.min) {
        state.min = value;
    }
    if (value > state.max) {
        state.max = value;
    }
}

/// @brief Merge two moment states with Chan's parallel merge formula.
///
/// Combines counts, means, M2 terms and extremes so a sharded instrument reports
/// the same moments as a single-threaded one. An empty operand is returned
/// unchanged as the result.
///
/// @param a Left moment state; not modified.
/// @param b Right moment state; not modified.
/// @return Merged state covering both inputs.
/// @note Spec `metrics/descriptive-statistics`, requirement "Multi-threaded
///       statistics merging".
[[nodiscard]] inline WelfordState welford_merge(const WelfordState& a,
                                                const WelfordState& b) noexcept {
    if (a.count == 0U) {
        return b;
    }
    if (b.count == 0U) {
        return a;
    }
    WelfordState out{};
    out.count = a.count + b.count;
    const double n = static_cast<double>(out.count);
    const double delta = b.mean - a.mean;
    out.mean = a.mean + delta * (static_cast<double>(b.count) / n);
    out.m2 = a.m2 + b.m2 +
             delta * delta * (static_cast<double>(a.count) * static_cast<double>(b.count) / n);
    out.min = (a.min < b.min) ? a.min : b.min;
    out.max = (a.max > b.max) ? a.max : b.max;
    return out;
}

} // namespace nexigen::metrics::detail
