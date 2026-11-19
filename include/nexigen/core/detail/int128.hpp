// nexigen::core — portable 128-bit arithmetic helpers.
//
// The time-base maps need a 64x64->128 product and a 128/64 division. The
// project forbids compiler extensions in public code, so the portable helpers
// are the contract; an `__int128` fast path is used only when the compiler
// provides it.
#pragma once

#include <cstdint>
#include <limits>

namespace nexigen::core::detail {

#if defined(__SIZEOF_INT128__)
/// @brief Compiler 128-bit signed integer when available.
__extension__ using int128 = __int128;
/// @brief Compiler 128-bit unsigned integer when available.
__extension__ using uint128 = unsigned __int128;
#else
/// @brief Portable 128-bit unsigned value.
struct uint128 {
    std::uint64_t hi{0}; ///< High 64 bits.
    std::uint64_t lo{0}; ///< Low 64 bits.
};
/// @brief Portable 128-bit signed value.
struct int128 {
    std::int64_t hi{0};  ///< High 64 bits (two's complement).
    std::uint64_t lo{0}; ///< Low 64 bits.
};
#endif

/// @brief Construct a 128-bit unsigned value from high and low halves.
/// @param hi High 64 bits.
/// @param lo Low 64 bits.
/// @return The combined value.
[[nodiscard]] inline uint128 make_u128(std::uint64_t hi, std::uint64_t lo) noexcept {
#if defined(__SIZEOF_INT128__)
    return (static_cast<uint128>(hi) << 64U) | lo;
#else
    return uint128{hi, lo};
#endif
}

/// @brief Portable 64x64 -> 128 unsigned product.
/// @param a First factor.
/// @param b Second factor.
/// @return The 128-bit product.
[[nodiscard]] inline uint128 multiply_u64(std::uint64_t a, std::uint64_t b) noexcept {
#if defined(__SIZEOF_INT128__)
    const uint128 product = static_cast<uint128>(a) * b;
    return static_cast<uint128>(product);
#else
    const std::uint64_t a_lo = a & 0xFFFF'FFFFULL;
    const std::uint64_t a_hi = a >> 32U;
    const std::uint64_t b_lo = b & 0xFFFF'FFFFULL;
    const std::uint64_t b_hi = b >> 32U;
    const std::uint64_t p0 = a_lo * b_lo;
    const std::uint64_t p1 = a_lo * b_hi;
    const std::uint64_t p2 = a_hi * b_lo;
    const std::uint64_t p3 = a_hi * b_hi;
    const std::uint64_t middle = (p0 >> 32U) + (p1 & 0xFFFF'FFFFULL) + (p2 & 0xFFFF'FFFFULL);
    uint128 result{};
    result.lo = (p0 & 0xFFFF'FFFFULL) | (middle << 32U);
    result.hi = p3 + (p1 >> 32U) + (p2 >> 32U) + (middle >> 32U);
    return result;
#endif
}

/// @brief Logical right shift of a 128-bit value.
/// @param value Value to shift.
/// @param shift Bit count; values of 128 or more yield zero.
/// @return Shifted low 64 bits.
[[nodiscard]] inline std::uint64_t shift_right_u128(uint128 value, unsigned shift) noexcept {
#if defined(__SIZEOF_INT128__)
    if (shift >= 128U) {
        return 0U;
    }
    return static_cast<std::uint64_t>(value >> shift);
#else
    if (shift >= 128U) {
        return 0U;
    }
    if (shift == 0U) {
        return value.lo;
    }
    if (shift < 64U) {
        return (value.lo >> shift) | (value.hi << (64U - shift));
    }
    if (shift == 64U) {
        return value.hi;
    }
    return value.hi >> (shift - 64U);
#endif
}

/// @brief Divide a 128-bit value by a 64-bit value.
///
/// The quotient is expected to fit 64 bits, which holds for every scale the
/// time base publishes.
///
/// @param numerator 128-bit numerator.
/// @param divisor Non-zero divisor.
/// @return Quotient low 64 bits.
[[nodiscard]] inline std::uint64_t divide_u128_u64(uint128 numerator,
                                                   std::uint64_t divisor) noexcept {
    if (divisor == 0U) {
        return 0U;
    }
#if defined(__SIZEOF_INT128__)
    return static_cast<std::uint64_t>(numerator / divisor);
#else
    std::uint64_t remainder = 0U;
    std::uint64_t quotient = 0U;
    for (int bit = 127; bit >= 0; --bit) {
        const std::uint64_t incoming =
            bit >= 64 ? ((numerator.hi >> (bit - 64)) & 1U) : ((numerator.lo >> bit) & 1U);
        remainder = (remainder << 1U) | incoming;
        if (remainder >= divisor) {
            remainder -= divisor;
            if (bit < 64) {
                quotient |= (1ULL << bit);
            }
        }
    }
    return quotient;
#endif
}

/// @brief Multiply a signed 64-bit value by an unsigned scale, then shift right.
///
/// @param value Signed value.
/// @param scale Unsigned Q-format scale.
/// @param shift Right shift applied after multiplication.
/// @return `(value * scale) >> shift`, clamped to the `int64` range.
[[nodiscard]] inline std::int64_t mul_shift_i64(std::int64_t value, std::uint64_t scale,
                                                unsigned shift) noexcept {
    const bool negative = value < 0;
    const std::uint64_t magnitude = negative ? static_cast<std::uint64_t>(-(value + 1)) + 1U
                                             : static_cast<std::uint64_t>(value);
    const uint128 product = multiply_u64(magnitude, scale);
    const std::uint64_t shifted = shift_right_u128(product, shift);
    if (negative) {
        constexpr std::uint64_t limit = 1ULL << 63U;
        if (shifted >= limit) {
            return std::numeric_limits<std::int64_t>::min();
        }
        return -static_cast<std::int64_t>(shifted);
    }
    if (shifted > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::numeric_limits<std::int64_t>::max();
    }
    return static_cast<std::int64_t>(shifted);
}

} // namespace nexigen::core::detail
