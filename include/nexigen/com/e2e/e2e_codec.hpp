// nexigen::com — E2E checksums and envelope codec (change add-e2e-protection).
//
// AUTOSAR polynomials, our framing. Table-driven, noexcept, allocation-free.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

#include <nexigen/com/e2e/e2e_message.hpp>
#include <nexigen/com/e2e/e2e_profile.hpp>
#include <nexigen/com/e2e/e2e_status.hpp>

namespace nexigen::com::e2e {

namespace detail {

/// @brief Build the CRC-32/AUTOSAR table (poly `0xF4ACFB13`, reflected).
[[nodiscard]] constexpr std::array<std::uint32_t, 256> make_crc32_table() noexcept {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t index = 0; index < 256U; ++index) {
        std::uint32_t value = index;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value & 1U) != 0U ? (value >> 1U) ^ 0xC8DF352FU : value >> 1U;
        }
        table[index] = value;
    }
    return table;
}

/// @brief Build the CRC-16/CCITT table (poly `0x1021`, non-reflected).
[[nodiscard]] constexpr std::array<std::uint16_t, 256> make_crc16_table() noexcept {
    std::array<std::uint16_t, 256> table{};
    for (std::uint32_t index = 0; index < 256U; ++index) {
        std::uint16_t value = static_cast<std::uint16_t>(index << 8U);
        for (int bit = 0; bit < 8; ++bit) {
            value = (value & 0x8000U) != 0U
                        ? static_cast<std::uint16_t>((static_cast<std::uint32_t>(value) << 1U) ^
                                                     0x1021U)
                        : static_cast<std::uint16_t>(static_cast<std::uint32_t>(value) << 1U);
        }
        table[index] = value;
    }
    return table;
}

inline constexpr std::array<std::uint32_t, 256> k_crc32_table = make_crc32_table();
inline constexpr std::array<std::uint16_t, 256> k_crc16_table = make_crc16_table();

/// @brief Byte offset of the CRC field inside `E2eHeader`.
inline constexpr std::size_t k_header_crc_offset = offsetof(E2eHeader, crc);

} // namespace detail

/// @brief Incremental CRC-32/AUTOSAR.
class Crc32P4 {
public:
    void update(std::span<const std::uint8_t> bytes) noexcept {
        std::uint32_t crc = state_;
        for (const std::uint8_t byte : bytes) {
            crc = detail::k_crc32_table[(crc ^ byte) & 0xffU] ^ (crc >> 8U);
        }
        state_ = crc;
    }

    [[nodiscard]] std::uint32_t finish() const noexcept { return state_ ^ 0xFFFFFFFFU; }

private:
    std::uint32_t state_{0xFFFFFFFFU};
};

/// @brief Incremental CRC-16/CCITT.
class Crc16 {
public:
    void update(std::span<const std::uint8_t> bytes) noexcept {
        std::uint16_t crc = state_;
        for (const std::uint8_t byte : bytes) {
            const std::uint8_t index = static_cast<std::uint8_t>(((crc >> 8U) ^ byte) & 0xffU);
            crc = static_cast<std::uint16_t>((static_cast<std::uint32_t>(crc) << 8U) ^
                                             detail::k_crc16_table[index]);
        }
        state_ = crc;
    }

    [[nodiscard]] std::uint16_t finish() const noexcept { return state_; }

private:
    std::uint16_t state_{0xFFFFU};
};

/// @brief One-shot CRC-32/AUTOSAR over bytes.
[[nodiscard]] inline std::uint32_t crc32p4(std::span<const std::uint8_t> bytes) noexcept {
    Crc32P4 crc;
    crc.update(bytes);
    return crc.finish();
}

/// @brief One-shot CRC-16/CCITT over bytes.
[[nodiscard]] inline std::uint16_t crc16(std::span<const std::uint8_t> bytes) noexcept {
    Crc16 crc;
    crc.update(bytes);
    return crc.finish();
}

/// @brief Counter mask of one profile.
[[nodiscard]] constexpr std::uint32_t counter_mask(E2eProfile profile) noexcept {
    switch (profile) {
        case E2eProfile::None: return 0U;
        case E2eProfile::P05:  return 0xFFU;
        case E2eProfile::P04:
        case E2eProfile::P06:  return 0xFFFFU;
        case E2eProfile::P07:  return 0xFFFFFFFFU;
    }
    return 0U;
}

/// @brief Checksum width of one profile (0 when disabled).
[[nodiscard]] constexpr std::uint32_t checksum_bits(E2eProfile profile) noexcept {
    switch (profile) {
        case E2eProfile::None: return 0U;
        case E2eProfile::P04:
        case E2eProfile::P07:  return 32U;
        case E2eProfile::P05:
        case E2eProfile::P06:  return 16U;
    }
    return 0U;
}

/// @brief Default byte view of a payload.
///
/// Returns the object representation, which is byte-stable for payloads
/// without padding. A payload with padding can provide its own
/// `e2e_bytes(const T&)` in its own namespace; the unqualified call in
/// `payload_bytes` finds it through ADL and prefers it over this template.
///
/// @param value Payload value.
/// @return Read-only bytes used by the checksum.
template <class T> [[nodiscard]] std::span<const std::uint8_t> e2e_bytes(const T& value) noexcept {
    return {reinterpret_cast<const std::uint8_t*>(&value), sizeof(T)};
}

/// @brief Byte view selected for checksums (ADL customization point).
template <class T>
[[nodiscard]] std::span<const std::uint8_t> payload_bytes(const T& value) noexcept {
    return e2e_bytes(value);
}

/// @brief Fill the header and checksum of one envelope.
template <concepts::Message T>
void seal_message(E2eMessage<T>& message, E2eProfile profile, std::uint32_t data_id,
                  std::uint32_t counter) noexcept {
    const auto payload = payload_bytes(message.payload);
    message.header.data_id = data_id;
    message.header.counter = counter & counter_mask(profile);
    message.header.length = static_cast<std::uint32_t>(payload.size());
    message.header.crc = 0U;
    if (profile == E2eProfile::None) {
        return;
    }
    const auto header = std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(&message.header), detail::k_header_crc_offset);
    if (checksum_bits(profile) == 32U) {
        Crc32P4 crc;
        crc.update(header);
        crc.update(payload);
        message.header.crc = crc.finish();
    } else {
        Crc16 crc;
        crc.update(header);
        crc.update(payload);
        message.header.crc = crc.finish();
    }
}

/// @brief Validate length, data id, and checksum of one envelope.
///
/// @return `true` when the envelope is intact; a counter check is separate.
template <concepts::Message T>
[[nodiscard]] bool check_message(const E2eMessage<T>& message, E2eProfile profile,
                                 std::uint32_t data_id) noexcept {
    if (profile == E2eProfile::None) {
        return true;
    }
    if (message.header.data_id != data_id) {
        return false;
    }
    const auto payload = payload_bytes(message.payload);
    if (message.header.length != payload.size()) {
        return false;
    }
    const auto header = std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(&message.header), detail::k_header_crc_offset);
    if (checksum_bits(profile) == 32U) {
        Crc32P4 crc;
        crc.update(header);
        crc.update(payload);
        return crc.finish() == message.header.crc;
    }
    Crc16 crc;
    crc.update(header);
    crc.update(payload);
    return static_cast<std::uint32_t>(crc.finish()) == (message.header.crc & 0xFFFFU);
}

} // namespace nexigen::com::e2e
