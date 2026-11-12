// nexigen::tracing — pinned FNV-1a 64-bit hash for cross-process source
// locations (change add-iceoryx2-trace-sink; spec
// tracing/iceoryx2-trace-sink "Source locations cross processes as hashes";
// design D3).
//
// The value is part of the wire contract: every process must agree on the
// hash of the same text independent of build flags, libc, or address space.
// `std::hash` is deliberately NOT used (its value is neither stable nor
// specified across processes). Offset basis and prime are the published
// FNV-1a 64 constants.
#pragma once

#include <cstdint>
#include <string_view>

namespace nexigen::tracing::detail {

/// @brief Published FNV-1a 64-bit offset basis (`0xcbf29ce484222325`).
///
/// Part of the wire contract for source-location hashes: every process must
/// start from the same value so equal text hashes equally.
///
/// @note Spec `tracing/iceoryx2-trace-sink`, requirement "Source locations
///       cross processes as hashes"; design D3.
inline constexpr std::uint64_t fnv1a_64_offset_basis = 0xcbf29ce484222325ULL;

/// @brief Published FNV-1a 64-bit prime (`0x100000001b3`).
///
/// Multiplier applied after each byte's XOR; pinned so the hash is portable
/// across builds, libc versions, and address spaces.
///
/// @note Spec `tracing/iceoryx2-trace-sink`, requirement "Source locations
///       cross processes as hashes"; design D3.
inline constexpr std::uint64_t fnv1a_64_prime = 0x100000001b3ULL;

/// @brief FNV-1a 64 over the bytes of `text`, without a trailing NUL.
///
/// Pure register arithmetic in a `constexpr` function: safe to evaluate at
/// compile time for static source locations and allocation-free at run time.
/// The value is the wire representation of a file or function name in
/// `SpanFrame`, so it must not depend on `std::hash` (whose value is neither
/// stable nor specified across processes).
///
/// @param text Bytes to hash; may be empty, in which case the offset basis is
///        returned.
/// @return The 64-bit FNV-1a digest of `text`.
/// @note Spec `tracing/iceoryx2-trace-sink`, requirement "Source locations
///       cross processes as hashes"; design D3.
[[nodiscard]] constexpr std::uint64_t fnv1a_64(std::string_view text) noexcept {
    std::uint64_t hash = fnv1a_64_offset_basis;
    for (const char ch : text) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(ch));
        hash *= fnv1a_64_prime;
    }
    return hash;
}

/// @brief Null-safe FNV-1a 64 overload for `source_location` accessors.
///
/// A missing location hashes to zero — the wire "unset" value — and is never
/// dereferenced.
///
/// @param text NUL-terminated text, or `nullptr` for an unset location.
/// @return Zero when `text` is `nullptr`, otherwise the FNV-1a digest of its
///         bytes (excluding the terminally added NUL).
/// @note Spec `tracing/iceoryx2-trace-sink`, requirement "Source locations
///       cross processes as hashes"; design D3.
[[nodiscard]] constexpr std::uint64_t fnv1a_64(const char* text) noexcept {
    return text == nullptr ? 0ULL : fnv1a_64(std::string_view{text});
}

} // namespace nexigen::tracing::detail
