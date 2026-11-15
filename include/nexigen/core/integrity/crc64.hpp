// nexigen::core::integrity — CRC-64/XZ (change add-integrity-primitives).
//
// Table-driven reflected CRC-64 with the XZ polynomial. One-shot and streaming
// forms, allocation-free and noexcept.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace nexigen::core::integrity {

namespace detail {

/// @brief Build the CRC-64/XZ table at compile time.
[[nodiscard]] constexpr std::array<std::uint64_t, 256> make_crc64_table() noexcept {
    std::array<std::uint64_t, 256> table{};
    for (std::uint32_t index = 0; index < 256U; ++index) {
        std::uint64_t value = index;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value & 1U) != 0U ? (value >> 1U) ^ 0xC96C5795D7870F42ULL : value >> 1U;
        }
        table[index] = value;
    }
    return table;
}

/// @brief Reflected XZ table shared by every translation unit.
inline constexpr std::array<std::uint64_t, 256> k_crc64_table = make_crc64_table();

} // namespace detail

/// @brief Incremental CRC-64/XZ over bytes or text.
///
/// @note Spec `core/integrity`, requirement "CRC-32C and CRC-64 primitives".
class Crc64 {
public:
    Crc64() noexcept = default;

    /// @brief Feed one byte buffer.
    void update(std::span<const std::uint8_t> bytes) noexcept {
        std::uint64_t crc = state_;
        for (const std::uint8_t byte : bytes) {
            crc = detail::k_crc64_table[(crc ^ byte) & 0xffU] ^ (crc >> 8U);
        }
        state_ = crc;
    }

    /// @brief Feed a text view.
    void update(std::string_view text) noexcept {
        update(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()),
                                             text.size()));
    }

    /// @brief Final checksum with the output XOR applied.
    [[nodiscard]] std::uint64_t finish() const noexcept { return state_ ^ 0xFFFFFFFFFFFFFFFFULL; }

private:
    std::uint64_t state_{0xFFFFFFFFFFFFFFFFULL}; ///< Running register.
};

/// @brief One-shot CRC-64/XZ over bytes.
[[nodiscard]] inline std::uint64_t crc64(std::span<const std::uint8_t> bytes) noexcept {
    Crc64 crc;
    crc.update(bytes);
    return crc.finish();
}

/// @brief One-shot CRC-64/XZ over text.
[[nodiscard]] inline std::uint64_t crc64(std::string_view text) noexcept {
    Crc64 crc;
    crc.update(text);
    return crc.finish();
}

} // namespace nexigen::core::integrity
