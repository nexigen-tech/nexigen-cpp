// nexigen storage-format benchmark — CRC-32C helpers.
//
// The benchmark compares the hardware-accelerated CRC-32C a production
// recorder could use with the table-driven core::integrity fallback. Both
// compute the same Castagnoli checksum.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <nexigen/core/integrity/crc32c.hpp>

#if defined(__x86_64__) && defined(__SSE4_2__)
#include <nmmintrin.h>
#define NEXIGEN_BENCH_HAS_SSE42_CRC 1
#else
#define NEXIGEN_BENCH_HAS_SSE42_CRC 0
#endif

namespace nexigen::bench {

/// @brief One-shot CRC-32C over bytes, using SSE4.2 when available.
[[nodiscard]] inline std::uint32_t crc32c_fast(std::span<const std::byte> bytes) noexcept {
#if NEXIGEN_BENCH_HAS_SSE42_CRC
    std::uint64_t crc = 0xFFFFFFFFULL;
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    std::size_t remaining = bytes.size();
    while (remaining >= sizeof(std::uint64_t)) {
        std::uint64_t word = 0;
        for (std::size_t i = 0; i < sizeof(word); ++i) {
            word |= static_cast<std::uint64_t>(data[i]) << (8U * i);
        }
        crc = _mm_crc32_u64(crc, word);
        data += sizeof(word);
        remaining -= sizeof(word);
    }
    while (remaining > 0) {
        crc = _mm_crc32_u8(static_cast<unsigned int>(crc), *data);
        ++data;
        --remaining;
    }
    return static_cast<std::uint32_t>(crc ^ 0xFFFFFFFFULL);
#else
    return core::integrity::crc32c(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
#endif
}

/// @brief One-shot CRC-32C over a byte range.
[[nodiscard]] inline std::uint32_t crc32c_fast(const void* data, std::size_t size) noexcept {
    return crc32c_fast(
        std::span<const std::byte>(static_cast<const std::byte*>(data), size));
}

} // namespace nexigen::bench
