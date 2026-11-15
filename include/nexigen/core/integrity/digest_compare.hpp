// nexigen::core::integrity — constant-time digest comparison (change
// add-integrity-primitives).
//
// Traverses the full length regardless of where the first difference sits. The
// helper is not advertised as a security boundary; it exists so callers do not
// write an early-exit memcmp by accident.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace nexigen::core::integrity {

namespace detail {

/// @brief Lowercase one ASCII hex character (other characters pass through).
[[nodiscard]] constexpr unsigned char fold_hex(unsigned char value) noexcept {
    return (value >= 'A' && value <= 'F') ? static_cast<unsigned char>(value + 32U) : value;
}

} // namespace detail

/// @brief Compare two hex digests without an early exit.
///
/// @param left First digest.
/// @param right Second digest.
/// @return `true` when the lengths are equal and every character matches,
///         comparing case-insensitively for hexadecimal letters.
/// @note Spec `core/integrity`, requirement "Constant-time digest comparison".
[[nodiscard]] inline bool digests_equal(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        difference |= static_cast<unsigned char>(
            detail::fold_hex(static_cast<unsigned char>(left[i])) ^
            detail::fold_hex(static_cast<unsigned char>(right[i])));
    }
    return difference == 0;
}

} // namespace nexigen::core::integrity
