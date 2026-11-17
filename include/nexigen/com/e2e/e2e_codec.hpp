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
///
/// @return A 256-entry lookup table for the reflected CRC-32 polynomial.
/// @note Spec `com/e2e`, requirement "E2E profile vocabulary"; `constexpr`, so
///       the table is built at compile time.
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
///
/// @return A 256-entry lookup table for the CRC-16/CCITT polynomial.
/// @note Spec `com/e2e`, requirement "E2E profile vocabulary"; `constexpr`, so
///       the table is built at compile time.
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

/// @brief Compile-time CRC-32/AUTOSAR lookup table.
inline constexpr std::array<std::uint32_t, 256> k_crc32_table = make_crc32_table();
/// @brief Compile-time CRC-16/CCITT lookup table.
inline constexpr std::array<std::uint16_t, 256> k_crc16_table = make_crc16_table();

/// @brief Byte offset of the CRC field inside `E2eHeader`.
///
/// The checksum covers every header byte before this offset plus the payload,
/// so the CRC field itself is excluded from the digest.
inline constexpr std::size_t k_header_crc_offset = offsetof(E2eHeader, crc);

} // namespace detail

/// @brief Incremental CRC-32/AUTOSAR.
///
/// Feed the header prefix and payload in any number of chunks, then read the
/// final reflected CRC with `finish()`. The state starts at `0xFFFFFFFF` and
/// `finish()` applies the final XOR.
///
/// @note Spec `com/e2e`, requirement "E2E profile vocabulary"; used by the
///       `P04` and `P07` profiles. No allocation, no exceptions.
class Crc32P4 {
public:
    /// @brief Feed the next byte range into the checksum.
    /// @param bytes Bytes to append to the running CRC.
    /// @note Allocation-free and `noexcept`; suitable for the sending path.
    void update(std::span<const std::uint8_t> bytes) noexcept {
        std::uint32_t crc = state_;
        for (const std::uint8_t byte : bytes) {
            crc = detail::k_crc32_table[(crc ^ byte) & 0xffU] ^ (crc >> 8U);
        }
        state_ = crc;
    }

    /// @brief Finalize the running CRC-32.
    /// @return The reflected CRC-32 with the final XOR applied.
    /// @note Does not mutate the incremental state.
    [[nodiscard]] std::uint32_t finish() const noexcept { return state_ ^ 0xFFFFFFFFU; }

private:
    std::uint32_t state_{0xFFFFFFFFU};
};

/// @brief Incremental CRC-16/CCITT.
///
/// Feed the header prefix and payload in any number of chunks, then read the
/// final non-reflected CRC with `finish()`. The state starts at `0xFFFF`.
///
/// @note Spec `com/e2e`, requirement "E2E profile vocabulary"; used by the
///       `P05` and `P06` profiles. No allocation, no exceptions.
class Crc16 {
public:
    /// @brief Feed the next byte range into the checksum.
    /// @param bytes Bytes to append to the running CRC.
    /// @note Allocation-free and `noexcept`; suitable for the sending path.
    void update(std::span<const std::uint8_t> bytes) noexcept {
        std::uint16_t crc = state_;
        for (const std::uint8_t byte : bytes) {
            const std::uint8_t index = static_cast<std::uint8_t>(((crc >> 8U) ^ byte) & 0xffU);
            crc = static_cast<std::uint16_t>((static_cast<std::uint32_t>(crc) << 8U) ^
                                             detail::k_crc16_table[index]);
        }
        state_ = crc;
    }

    /// @brief Finalize the running CRC-16.
    /// @return The CRC-16/CCITT value.
    /// @note Does not mutate the incremental state.
    [[nodiscard]] std::uint16_t finish() const noexcept { return state_; }

private:
    std::uint16_t state_{0xFFFFU};
};

/// @brief One-shot CRC-32/AUTOSAR over bytes.
///
/// @param bytes Complete byte range to checksum.
/// @return The reflected CRC-32 with the final XOR applied.
/// @note Spec `com/e2e`, requirement "E2E envelope and checksum";
///       allocation-free and `noexcept`.
[[nodiscard]] inline std::uint32_t crc32p4(std::span<const std::uint8_t> bytes) noexcept {
    Crc32P4 crc;
    crc.update(bytes);
    return crc.finish();
}

/// @brief One-shot CRC-16/CCITT over bytes.
///
/// @param bytes Complete byte range to checksum.
/// @return The CRC-16/CCITT value.
/// @note Spec `com/e2e`, requirement "E2E envelope and checksum";
///       allocation-free and `noexcept`.
[[nodiscard]] inline std::uint16_t crc16(std::span<const std::uint8_t> bytes) noexcept {
    Crc16 crc;
    crc.update(bytes);
    return crc.finish();
}

/// @brief Counter mask of one profile.
///
/// @param profile Selected protection profile.
/// @return The mask applied to the sender counter (`0xFF`, `0xFFFF`, or
///         `0xFFFFFFFF`); `0` when protection is disabled.
/// @note Spec `com/e2e`, requirement "Sender counter progression".
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
///
/// @param profile Selected protection profile.
/// @return `32` for `P04`/`P07`, `16` for `P05`/`P06`, or `0` for `None`.
/// @note Spec `com/e2e`, requirement "E2E profile vocabulary".
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
/// @tparam T Payload type; normally a `com::concepts::Message`.
/// @param value Payload value.
/// @return Read-only bytes used by the checksum.
/// @note Spec `com/e2e`, requirement "E2E envelope and checksum".
template <class T> [[nodiscard]] std::span<const std::uint8_t> e2e_bytes(const T& value) noexcept {
    return {reinterpret_cast<const std::uint8_t*>(&value), sizeof(T)};
}

/// @brief Byte view selected for checksums (ADL customization point).
///
/// @tparam T Payload type.
/// @param value Payload value.
/// @return Bytes returned by the payload's `e2e_bytes` overload, or the
///         default object representation.
/// @note Spec `com/e2e`, requirement "E2E envelope and checksum"; payloads
///       with padding can specialize `e2e_bytes` without changing the codec.
template <class T>
[[nodiscard]] std::span<const std::uint8_t> payload_bytes(const T& value) noexcept {
    return e2e_bytes(value);
}

/// @brief Fill the header and checksum of one envelope.
///
/// Uses the profile's counter width and checksum width, computes the digest
/// over the header prefix and payload bytes, and writes the result into
/// `message.header.crc`.
///
/// @tparam T Payload type satisfying `com::concepts::Message`.
/// @param message Envelope to seal in place.
/// @param profile Selected protection profile.
/// @param data_id Service-specific identity written into the header.
/// @param counter Sender counter value before masking.
/// @note Spec `com/e2e`, requirements "E2E envelope and checksum" and
///       "Sender counter progression"; allocation-free and `noexcept`.
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
/// @tparam T Payload type satisfying `com::concepts::Message`.
/// @param message Envelope to validate.
/// @param profile Selected protection profile.
/// @param data_id Expected service identity.
/// @return `true` when the envelope is intact; a counter check is separate.
/// @note Spec `com/e2e`, requirement "E2E envelope and checksum"; `None` makes
///       the check a no-op. Allocation-free and `noexcept`.
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
