// nexigen::chrono — device-domain estimator implementation (change
// add-time-base-service; spec chrono/time-base "Domain coefficient row";
// design D-F).
//
// Cold path only: integer arithmetic over at most 64 pairs, no allocation.
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>

#include <nexigen/chrono/time_base_estimator.hpp>

namespace nexigen::chrono {
namespace {

/// @brief Absolute value of a signed 64-bit value without overflow.
///
/// @param value Value to reduce.
/// @return Non-negative magnitude.
[[nodiscard]] constexpr std::uint64_t magnitude(std::int64_t value) noexcept {
    return value < 0 ? static_cast<std::uint64_t>(-(value + 1)) + 1U
                     : static_cast<std::uint64_t>(value);
}

} // namespace

TimeBaseEstimator::Window TimeBaseEstimator::window() const noexcept {
    Window summary{};
    summary.min_native = std::numeric_limits<std::uint64_t>::max();
    summary.min_canonical = std::numeric_limits<std::int64_t>::max();
    summary.max_canonical = std::numeric_limits<std::int64_t>::min();
    for (std::size_t i = 0; i < count_; ++i) {
        const Sample& sample = samples_[i];
        if (sample.native < summary.min_native) {
            summary.min_native = sample.native;
            summary.min_native_canonical = sample.canonical;
        }
        if (sample.native > summary.max_native) {
            summary.max_native = sample.native;
            summary.max_native_canonical = sample.canonical;
        }
        if (sample.canonical < summary.min_canonical) {
            summary.min_canonical = sample.canonical;
        }
        if (sample.canonical > summary.max_canonical) {
            summary.max_canonical = sample.canonical;
        }
    }
    return summary;
}

std::optional<TimeBaseEstimator::Fit> TimeBaseEstimator::fit(const Window& summary) const noexcept {
    if (summary.max_native == summary.min_native) {
        return std::nullopt;
    }
    const std::int64_t canonical_span = summary.max_native_canonical - summary.min_native_canonical;
    if (canonical_span <= 0) {
        return std::nullopt; // the clock did not advance; cannot fit a map
    }
    const std::uint64_t native_span = summary.max_native - summary.min_native;
    const nexigen::core::detail::uint128 numerator = nexigen::core::detail::multiply_u64(
        static_cast<std::uint64_t>(canonical_span), 1ULL << 32U);
    const std::uint64_t scale_fwd_q32 =
        nexigen::core::detail::divide_u128_u64(numerator, native_span);
    if (scale_fwd_q32 == 0U) {
        return std::nullopt; // a counter slower than one tick per megananosecond
    }

    Fit result{};
    result.scale_fwd_q32 = scale_fwd_q32;
    result.min_residual = std::numeric_limits<std::uint64_t>::max();
    for (std::size_t i = 0; i < count_; ++i) {
        const Sample& sample = samples_[i];
        const std::int64_t predicted = detail::map_forward(
            sample.native, summary.min_native, summary.min_native_canonical, scale_fwd_q32);
        const std::uint64_t residual = magnitude(sample.canonical - predicted);
        if (residual < result.min_residual) {
            result.min_residual = residual;
            result.anchor = i;
        }
        if (residual > result.max_residual) {
            result.max_residual = residual;
        }
    }
    return result;
}

std::optional<DomainCoefficient>
TimeBaseEstimator::estimate(std::uint16_t domain_id, std::uint64_t epoch,
                            std::uint64_t max_residual_ns) const noexcept {
    if (count_ < 2U) {
        return std::nullopt;
    }
    const Window summary = window();
    const std::optional<Fit> fitted = fit(summary);
    if (!fitted) {
        return std::nullopt;
    }

    DomainCoefficient row{};
    row.domain_id = domain_id;
    row.domain_class = TimeDomainClass::DeviceClock;
    row.epoch = epoch;
    row.native_anchor = samples_[fitted->anchor].native;
    row.canonical_anchor_ns = samples_[fitted->anchor].canonical;
    row.scale_fwd_q32 = fitted->scale_fwd_q32;
    row.scale_rev_q32 = detail::reciprocal_scale_q32(fitted->scale_fwd_q32);
    row.flags |= DomainCoefficient::has_reverse_bit;
    row.unc_ns = fitted->max_residual > fitted->min_residual
                     ? (fitted->max_residual - fitted->min_residual) + 1U
                     : 1U;
    row.lock = row.unc_ns <= max_residual_ns ? LockState::Locked : LockState::Holdover;
    row.valid_from_native = summary.min_native;
    row.valid_to_native = summary.max_native;
    row.valid_from_canonical = static_cast<std::uint64_t>(summary.min_canonical);
    row.valid_to_canonical = static_cast<std::uint64_t>(summary.max_canonical);
    return row;
}

bool TimeBaseEstimator::is_significant_change(const DomainCoefficient& candidate,
                                              const DomainCoefficient& published,
                                              std::uint64_t uncertainty_slack_ns) noexcept {
    if (candidate.domain_id != published.domain_id || candidate.epoch != published.epoch ||
        candidate.lock != published.lock || candidate.domain_class != published.domain_class) {
        return true;
    }
    const std::uint64_t scale_difference =
        magnitude(static_cast<std::int64_t>(candidate.scale_fwd_q32) -
                  static_cast<std::int64_t>(published.scale_fwd_q32));
    if (scale_difference > 0U &&
        scale_difference * 1'000'000U > published.scale_fwd_q32) { // > 1 ppm
        return true;
    }
    // Anchor drift is judged in canonical nanoseconds through the published map.
    const std::int64_t predicted = canonical_from_native(published, candidate.native_anchor);
    const std::uint64_t drift = magnitude(candidate.canonical_anchor_ns - predicted);
    return drift > published.unc_ns + uncertainty_slack_ns;
}

std::string TimeBaseEstimator::to_string() const {
    std::string last = "none";
    if (count_ > 0U) {
        const Sample& sample = samples_[(next_ + max_samples - 1U) % max_samples];
        last = std::format("native={}, canonical={}", sample.native, sample.canonical);
    }
    return std::format("TimeBaseEstimator(samples={}, last=({}))", count_, last);
}

std::string to_string(const TimeBaseEstimator& estimator) {
    return estimator.to_string();
}

} // namespace nexigen::chrono
