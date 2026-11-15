// nexigen::com — E2E wire header and message envelope (change
// add-e2e-protection).
#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <type_traits>

#include <nexigen/com/concepts/message_concept.hpp>

namespace nexigen::com {

/// @brief Fixed 16-byte E2E header carried beside the payload.
///
/// Our framing (not byte-compatible with AUTOSAR wire profiles): `data_id`,
/// `counter`, `length`, and `crc`. The checksum covers the first 12 bytes and
/// the payload bytes.
///
/// @note Spec `com/e2e`, requirement "E2E envelope and checksum".
struct E2eHeader {
    std::uint32_t data_id{0}; ///< Service-specific identity.
    std::uint32_t counter{0}; ///< Sender counter, width selected by the profile.
    std::uint32_t length{0};  ///< Payload byte count.
    std::uint32_t crc{0};     ///< Checksum; low 16 bits for CRC-16 profiles.

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const {
        return std::format("E2eHeader(data_id={}, counter={}, length={}, crc={:#x})", data_id,
                           counter, length, crc);
    }
};

static_assert(sizeof(E2eHeader) == 16U, "the E2E header must stay 16 bytes");

/// @brief Trivially copyable payload envelope.
///
/// Travels as an ordinary `com::concepts::Message`, so the existing publisher,
/// subscriber, client, and server templates carry it without changes.
///
/// @tparam T Payload type satisfying `com::concepts::Message`.
/// @note Spec `com/e2e`, requirement "E2E envelope and checksum".
template <concepts::Message T> struct E2eMessage {
    E2eHeader header{}; ///< Wire header.
    T payload{};        ///< Application payload.
};

template <concepts::Message T>
constexpr bool k_e2e_message_is_trivially_copyable = std::is_trivially_copyable_v<E2eMessage<T>>;

/// @brief Free helper mirroring `E2eHeader::to_string()` (mandate 9).
[[nodiscard]] inline std::string to_string(const E2eHeader& header) {
    return header.to_string();
}

} // namespace nexigen::com

/// @brief `std::formatter` for the E2E header (project mandate 11).
template <> struct std::formatter<nexigen::com::E2eHeader> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::com::E2eHeader& header, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(header.to_string(), ctx);
    }
};
