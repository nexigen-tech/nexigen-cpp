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

/// Plain moment state (the observable summary shape).
struct Welford_state {
    std::uint64_t count{0};
    double mean{0.0};
    double m2{0.0};
    double min{0.0};
    double max{0.0};
};

/// Population standard deviation of a moment state.
[[nodiscard]] inline double population_std(const Welford_state& state) noexcept {
    if (state.count == 0U) {
        return 0.0;
    }
    return std::sqrt(state.m2 / static_cast<double>(state.count));
}

/// Fold one sample into a moment state (Welford). The caller owns the state;
/// no allocation, no locks.
inline void welford_add(Welford_state& state, double value) noexcept {
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

/// Merge two moment states (Chan et al. parallel merge).
[[nodiscard]] inline Welford_state welford_merge(const Welford_state& a,
                                                  const Welford_state& b) noexcept {
    if (a.count == 0U) {
        return b;
    }
    if (b.count == 0U) {
        return a;
    }
    Welford_state out{};
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
