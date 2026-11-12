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

inline constexpr std::uint64_t fnv1a_64_offset_basis = 0xcbf29ce484222325ULL;
inline constexpr std::uint64_t fnv1a_64_prime = 0x100000001b3ULL;

/// FNV-1a 64 over the bytes of `text`, WITHOUT a trailing NUL.
[[nodiscard]] constexpr std::uint64_t fnv1a_64(std::string_view text) noexcept {
    std::uint64_t hash = fnv1a_64_offset_basis;
    for (const char ch : text) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(ch));
        hash *= fnv1a_64_prime;
    }
    return hash;
}

/// Null-safe overload for the `source_location` accessors: a missing location
/// hashes to zero (the wire "unset" value) and never reads memory.
[[nodiscard]] constexpr std::uint64_t fnv1a_64(const char* text) noexcept {
    return text == nullptr ? 0ULL : fnv1a_64(std::string_view{text});
}

} // namespace nexigen::tracing::detail
