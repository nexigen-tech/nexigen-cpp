// nexigen::chrono — Q32 anchored forward/reverse domain maps.
//
// The map uses the shared anchor pair and Q32 scales:
//   hub  = c_anchor + ((native - n_anchor) * scale_fwd_q32 >> 32)
//   native = n_anchor + ((hub  - c_anchor) * scale_rev_q32 >> 32)
// Both products go through core::detail::int128.
#pragma once

#include <cstdint>
#include <limits>

#include <nexigen/core/detail/int128.hpp>

namespace nexigen::chrono::detail {

/// @brief Q32 fixed-point fractional bits.
inline constexpr unsigned domain_scale_shift = 32U;

/// @brief Forward anchored map: native -> hub nanoseconds.
///
/// @param native Native value.
/// @param native_anchor Native anchor.
/// @param canonical_anchor Hub anchor in nanoseconds.
/// @param scale_fwd_q32 Hub nanoseconds per native unit in Q32.
/// @return Hub nanoseconds, clamped to the int64 range.
[[nodiscard]] inline std::int64_t map_forward(std::uint64_t native, std::uint64_t native_anchor,
                                              std::int64_t canonical_anchor,
                                              std::uint64_t scale_fwd_q32) noexcept {
    const std::int64_t delta = static_cast<std::int64_t>(native - native_anchor);
    const std::int64_t scaled =
        nexigen::core::detail::mul_shift_i64(delta, scale_fwd_q32, domain_scale_shift);
    const std::int64_t anchor = canonical_anchor;
    if (scaled > 0 && anchor > std::numeric_limits<std::int64_t>::max() - scaled) {
        return std::numeric_limits<std::int64_t>::max();
    }
    if (scaled < 0 && anchor < std::numeric_limits<std::int64_t>::min() - scaled) {
        return std::numeric_limits<std::int64_t>::min();
    }
    return anchor + scaled;
}

/// @brief Reverse anchored map: hub nanoseconds -> native.
///
/// @param canonical Hub value in nanoseconds.
/// @param canonical_anchor Hub anchor in nanoseconds.
/// @param native_anchor Native anchor.
/// @param scale_rev_q32 Native units per hub nanosecond in Q32.
/// @return Native value, saturated at the uint64 range.
[[nodiscard]] inline std::uint64_t map_reverse(std::int64_t canonical,
                                               std::int64_t canonical_anchor,
                                               std::uint64_t native_anchor,
                                               std::uint64_t scale_rev_q32) noexcept {
    const std::int64_t delta = canonical - canonical_anchor;
    const std::int64_t scaled =
        nexigen::core::detail::mul_shift_i64(delta, scale_rev_q32, domain_scale_shift);
    if (scaled < 0) {
        const std::uint64_t magnitude = static_cast<std::uint64_t>(-(scaled + 1)) + 1U;
        return native_anchor > magnitude ? native_anchor - magnitude : 0U;
    }
    const std::uint64_t addition = static_cast<std::uint64_t>(scaled);
    if (native_anchor > std::numeric_limits<std::uint64_t>::max() - addition) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return native_anchor + addition;
}

/// @brief Reciprocal Q32 scale: native units per hub nanosecond.
///
/// @param scale_fwd_q32 Hub nanoseconds per native unit in Q32.
/// @return Reciprocal scale, or zero for a non-positive input.
[[nodiscard]] inline std::uint64_t reciprocal_scale_q32(std::uint64_t scale_fwd_q32) noexcept {
    if (scale_fwd_q32 == 0U) {
        return 0U;
    }
    return nexigen::core::detail::divide_u128_u64(nexigen::core::detail::make_u128(1U, 0U),
                                                  scale_fwd_q32);
}

} // namespace nexigen::chrono::detail
