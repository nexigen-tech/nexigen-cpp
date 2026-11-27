// nexigen::canopen — one unpacked PDO signal.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/canopen/eds_data_kind.hpp>

namespace nexigen::canopen {

/// @brief One signal extracted from a PDO payload.
struct PdoSignal {
    std::uint16_t index{0};                 ///< Mapped object index.
    std::uint8_t subindex{0};               ///< Mapped object subindex.
    std::uint8_t bits{0};                   ///< Signal width in bits.
    EdsDataKind kind{EdsDataKind::Unknown}; ///< Interpretation from the dictionary.
    std::uint64_t raw{0};                   ///< Unsigned raw value.
    std::int64_t signed_value{0};           ///< Signed interpretation.
    double real{0.0};                       ///< REAL32/REAL64 interpretation.
    bool valid{false};                      ///< Whether extraction fit the payload.
    bool unknown_object{false};             ///< Mapping target absent from the dictionary.
    bool string_value{false};               ///< Payload bytes are textual/octet data.
    std::array<std::uint8_t, 64> bytes{};   ///< Extracted string/octet bytes.
    std::uint8_t byte_count{0};             ///< Valid bytes in `bytes`.

    /// @brief Cold-path diagnostic rendering.
    /// @return One-line signal summary.
    [[nodiscard]] std::string to_string() const {
        return std::format("PdoSignal(index=0x{:04x}, sub={}, bits={}, kind={}, raw=0x{:x})", index,
                           subindex, bits, nexigen::canopen::to_string(kind), raw);
    }
};

/// @brief Free helper mirroring `PdoSignal::to_string`.
[[nodiscard]] inline std::string to_string(const PdoSignal& signal) {
    return signal.to_string();
}

/// @brief Serialize one PDO signal.
void to_json(nlohmann::json& j, const PdoSignal& signal);
/// @brief Parse one PDO signal without throwing.
void from_json(const nlohmann::json& j, PdoSignal& signal) noexcept;

} // namespace nexigen::canopen
