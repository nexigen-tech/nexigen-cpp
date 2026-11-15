// nexigen::core::integrity — CRC-32C (change add-integrity-primitives).
//
// Table-driven Castagnoli CRC. One-shot and streaming forms, allocation-free,
// noexcept, and usable from any module without a compiled dependency.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace nexigen::core::integrity {

namespace detail {

/// @brief Build the CRC-32C table at compile time.
[[nodiscard]] constexpr std::array<std::uint32_t, 256> make_crc32c_table() noexcept {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t index = 0; index < 256U; ++index) {
        std::uint32_t value = index;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value & 1U) != 0U ? (value >> 1U) ^ 0x82F63B78U : value >> 1U;
        }
        table[index] = value;
    }
    return table;
}

/// @brief Reflected Castagnoli table shared by every translation unit.
inline constexpr std::array<std::uint32_t, 256> k_crc32c_table = make_crc32c_table();

} // namespace detail

/// @brief Incremental CRC-32C over bytes or text.
///
/// @note Spec `core/integrity`, requirement "CRC-32C and CRC-64 primitives".
class Crc32c {
public:
    Crc32c() noexcept = default;

    /// @brief Feed one byte buffer.
    void update(std::span<const std::uint8_t> bytes) noexcept {
        std::uint32_t crc = state_;
        for (const std::uint8_t byte : bytes) {
            crc = detail::k_crc32c_table[(crc ^ byte) & 0xffU] ^ (crc >> 8U);
        }
        state_ = crc;
    }

    /// @brief Feed a text view.
    void update(std::string_view text) noexcept {
        update(std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
    }

    /// @brief Final checksum with the output XOR applied.
    [[nodiscard]] std::uint32_t finish() const noexcept { return state_ ^ 0xFFFFFFFFU; }

private:
    std::uint32_t state_{0xFFFFFFFFU}; ///< Running register.
};

/// @brief One-shot CRC-32C over bytes.
[[nodiscard]] inline std::uint32_t crc32c(std::span<const std::uint8_t> bytes) noexcept {
    Crc32c crc;
    crc.update(bytes);
    return crc.finish();
}

/// @brief One-shot CRC-32C over text.
[[nodiscard]] inline std::uint32_t crc32c(std::string_view text) noexcept {
    Crc32c crc;
    crc.update(text);
    return crc.finish();
}

} // namespace nexigen::core::integrity
