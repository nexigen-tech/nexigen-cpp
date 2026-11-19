// nexigen::chrono — fixed-point two-state Kalman filter implementation.
#include <cstdint>
#include <limits>

#include <nexigen/chrono/detail/kalman2.hpp>
#include <nexigen/core/detail/int128.hpp>

namespace nexigen::chrono::detail {
namespace {

/// @brief Q12 fractional bits used by the covariance entries.
constexpr unsigned kQ12 = 12U;
/// @brief Gain fractional bits (Q20 keeps sub-unit gains representable).
constexpr unsigned kQ20 = 20U;
/// @brief Nanoseconds per second.
constexpr std::int64_t kNanosecondsPerSecond = 1'000'000'000LL;

/// @brief Multiply two signed values and shift right, saturating.
///
/// @param lhs First factor.
/// @param rhs Second factor.
/// @param shift Right shift applied after the product.
/// @return `(lhs * rhs) >> shift`, clamped to the `int64` range.
[[nodiscard]] std::int64_t mul_shift(std::int64_t lhs, std::int64_t rhs, unsigned shift) noexcept {
    const bool negative = (lhs < 0) != (rhs < 0);
    const std::uint64_t left =
        lhs < 0 ? static_cast<std::uint64_t>(-(lhs + 1)) + 1U : static_cast<std::uint64_t>(lhs);
    const std::uint64_t right =
        rhs < 0 ? static_cast<std::uint64_t>(-(rhs + 1)) + 1U : static_cast<std::uint64_t>(rhs);
    const std::uint64_t product = nexigen::core::detail::shift_right_u128(
        nexigen::core::detail::multiply_u64(left, right), shift);
    if (negative) {
        constexpr std::uint64_t limit = 1ULL << 63U;
        if (product >= limit) {
            return std::numeric_limits<std::int64_t>::min();
        }
        return -static_cast<std::int64_t>(product);
    }
    if (product > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::numeric_limits<std::int64_t>::max();
    }
    return static_cast<std::int64_t>(product);
}

/// @brief Add with saturation.
[[nodiscard]] std::int64_t add_sat(std::int64_t lhs, std::int64_t rhs) noexcept {
    if (rhs > 0 && lhs > std::numeric_limits<std::int64_t>::max() - rhs) {
        return std::numeric_limits<std::int64_t>::max();
    }
    if (rhs < 0 && lhs < std::numeric_limits<std::int64_t>::min() - rhs) {
        return std::numeric_limits<std::int64_t>::min();
    }
    return lhs + rhs;
}

/// @brief Convert a nanosecond interval into Q12 seconds.
[[nodiscard]] std::int64_t dt_to_q12_seconds(std::uint64_t dt_ns) noexcept {
    if (dt_ns >= static_cast<std::uint64_t>(kNanosecondsPerSecond)) {
        const std::uint64_t seconds = dt_ns / static_cast<std::uint64_t>(kNanosecondsPerSecond);
        if (seconds >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() >> kQ12)) {
            return std::numeric_limits<std::int64_t>::max();
        }
        return static_cast<std::int64_t>(seconds) << kQ12;
    }
    return static_cast<std::int64_t>((dt_ns << kQ12) /
                                     static_cast<std::uint64_t>(kNanosecondsPerSecond));
}

} // namespace

void Kalman2::reset(std::int64_t offset_ns) noexcept {
    offset_ns_ = offset_ns;
    drift_q20_ = 0;
    p00_ = config_.measurement_ns2 << kQ12;
    p01_ = 0;
    p11_ = config_.process_drift_q12;
    last_dt_ns_ = 0U;
    initialized_ = true;
}

void Kalman2::update(std::int64_t measured_offset_ns, std::uint64_t dt_ns) noexcept {
    if (!initialized_) {
        reset(measured_offset_ns);
        last_dt_ns_ = dt_ns;
        return;
    }
    const std::uint64_t interval = dt_ns == 0U ? last_dt_ns_ : dt_ns;
    last_dt_ns_ = interval;
    const std::int64_t dt_q12 = dt_to_q12_seconds(interval);

    // Predict: offset += drift * dt, P = F P F^T + Q.
    offset_ns_ = add_sat(offset_ns_, mul_shift(drift_q20_, dt_q12, kQ12 + kQ20));
    const std::int64_t two_dt_p01 = 2 * mul_shift(dt_q12, p01_, kQ12);
    const std::int64_t dt_p11 = mul_shift(dt_q12, p11_, kQ12);
    p00_ = add_sat(p00_, add_sat(two_dt_p01, mul_shift(dt_q12, dt_p11, kQ12)));
    p00_ = add_sat(p00_, config_.process_offset_ns2 << kQ12);
    p01_ = add_sat(p01_, dt_p11);
    p11_ = add_sat(p11_, config_.process_drift_q12);

    // Update: S = P00 + R, K = P / S.
    const std::int64_t innovation = measured_offset_ns - offset_ns_;
    const std::int64_t s = add_sat(p00_, config_.measurement_ns2 << kQ12);
    if (s <= 0) {
        return;
    }
    const std::int64_t k0 = (p00_ << kQ20) / s;
    const std::int64_t k1 = (p01_ << kQ20) / s;
    offset_ns_ = add_sat(offset_ns_, mul_shift(k0, innovation, kQ20));
    drift_q20_ = add_sat(drift_q20_, mul_shift(k1, innovation, 0U));
    p00_ = add_sat(p00_, -mul_shift(k0, p00_, kQ20));
    p01_ = add_sat(p01_, -mul_shift(k0, p01_, kQ20));
    p11_ = add_sat(p11_, -mul_shift(k1, p01_, kQ20));
    if (p00_ < 0) {
        p00_ = 0;
    }
    if (p11_ < 0) {
        p11_ = 0;
    }
}

std::int64_t Kalman2::drift_ppb() const noexcept {
    // 1 ns/s is 1e9 ppb; `mul_shift` keeps the product in 128 bits.
    return mul_shift(drift_q20_, 1'000'000'000LL, kQ20);
}

std::int64_t Kalman2::predict_offset_ns(std::uint64_t horizon_ns) const noexcept {
    if (!initialized_) {
        return offset_ns_;
    }
    if (horizon_ns > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return offset_ns_;
    }
    const std::int64_t horizon = static_cast<std::int64_t>(horizon_ns);
    const std::int64_t delta = mul_shift(drift_q20_, horizon, kQ20) / kNanosecondsPerSecond;
    return add_sat(offset_ns_, delta);
}

} // namespace nexigen::chrono::detail
