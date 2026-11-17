// nexigen::recorder::detail — CRC-32C acceleration for the storage writer.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include <nexigen/core/integrity/crc32c.hpp>

// The hardware bodies are compiled once per translation unit but selected at
// run time, so a Debug build on an SSE4.2 CPU still uses the fast path.
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#include <nmmintrin.h>
#define NEXIGEN_RECORDER_X86_CRC 1
#else
#define NEXIGEN_RECORDER_X86_CRC 0
#endif

// AArch64 exposes the CRC32 extension through ACLE when the toolchain enables
// `+crc`; builds without it keep the portable table below.
#if defined(__aarch64__) && defined(__ARM_FEATURE_CRC32)
#include <arm_acle.h>
#define NEXIGEN_RECORDER_ARM_CRC 1
#else
#define NEXIGEN_RECORDER_ARM_CRC 0
#endif

namespace nexigen::recorder::detail {

#if NEXIGEN_RECORDER_X86_CRC
/// @brief SSE4.2 body; call only when `__builtin_cpu_supports("sse4.2")`.
[[nodiscard]] __attribute__((target("sse4.2"))) inline std::uint32_t
crc32c_sse42(std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t crc = 0xFFFFFFFFU;
    const auto* data = bytes.data();
    std::size_t remaining = bytes.size();
    while (remaining >= 8U) {
        std::uint64_t word = 0;
        std::memcpy(&word, data, sizeof(word));
        crc = static_cast<std::uint32_t>(_mm_crc32_u64(crc, word));
        data += 8U;
        remaining -= 8U;
    }
    while (remaining > 0U) {
        crc = _mm_crc32_u8(crc, *data);
        ++data;
        --remaining;
    }
    return crc ^ 0xFFFFFFFFU;
}
#endif

#if NEXIGEN_RECORDER_ARM_CRC
/// @brief ARMv8 CRC32C body, available when the build enables `+crc`.
[[nodiscard]] inline std::uint32_t crc32c_aarch64(std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t crc = 0xFFFFFFFFU;
    const auto* data = bytes.data();
    std::size_t remaining = bytes.size();
    while (remaining >= 8U) {
        std::uint64_t word = 0;
        std::memcpy(&word, data, sizeof(word));
        crc = __crc32cd(crc, word);
        data += 8U;
        remaining -= 8U;
    }
    while (remaining > 0U) {
        crc = __crc32cb(crc, *data);
        ++data;
        --remaining;
    }
    return crc ^ 0xFFFFFFFFU;
}
#endif

/// @brief One-shot CRC-32C using the hardware path when this CPU has one.
[[nodiscard]] inline std::uint32_t crc32c(std::span<const std::uint8_t> bytes) noexcept {
#if NEXIGEN_RECORDER_X86_CRC
    if (__builtin_cpu_supports("sse4.2") != 0) {
        return crc32c_sse42(bytes);
    }
#elif NEXIGEN_RECORDER_ARM_CRC
    return crc32c_aarch64(bytes);
#endif
    return core::integrity::crc32c(bytes);
}

[[nodiscard]] inline std::uint32_t crc32c(const void* data, std::size_t size) noexcept {
    return crc32c(std::span<const std::uint8_t>(static_cast<const std::uint8_t*>(data), size));
}

} // namespace nexigen::recorder::detail
