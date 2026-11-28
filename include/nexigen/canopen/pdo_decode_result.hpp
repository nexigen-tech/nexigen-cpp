// nexigen::canopen — PDO decode result.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/canopen/pdo_signal.hpp>
#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::canopen {

/// @brief Outcome of unpacking one PDO.
enum class PdoDecodeStatus : std::uint8_t {
    Invalid,       ///< Not a PDO payload.
    Ok,            ///< Fully decoded.
    Partial,       ///< Payload ended before the mapping.
    UnknownObject, ///< Mapping references an object absent from the dictionary.
};

} // namespace nexigen::canopen

namespace nexigen::core {

/// @brief Vocabulary table for `PdoDecodeStatus`.
template <> struct EnumTraits<nexigen::canopen::PdoDecodeStatus> {
    /// @brief One value/name row.
    struct Entry {
        nexigen::canopen::PdoDecodeStatus value; ///< Enumerator.
        std::string_view name;                   ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::canopen::PdoDecodeStatus::Invalid, "Invalid"},
        {nexigen::canopen::PdoDecodeStatus::Ok, "Ok"},
        {nexigen::canopen::PdoDecodeStatus::Partial, "Partial"},
        {nexigen::canopen::PdoDecodeStatus::UnknownObject, "UnknownObject"},
    }};
};

} // namespace nexigen::core

namespace nexigen::canopen {

/// @brief Result of unpacking one PDO payload.
struct PdoDecodeResult {
    static constexpr std::size_t max_signals{16U};

    PdoDecodeStatus status{PdoDecodeStatus::Invalid}; ///< Decode outcome.
    std::array<PdoSignal, max_signals> signals{};     ///< Decoded signals.
    std::uint8_t count{0};                            ///< Valid signal entries.

    /// @brief Cold-path diagnostic rendering.
    /// @return One-line result summary.
    [[nodiscard]] std::string to_string() const {
        return std::format("PdoDecodeResult(status={}, count={})", core::enum_name(status), count);
    }
};

/// @brief Free helper mirroring `PdoDecodeResult::to_string`.
[[nodiscard]] inline std::string to_string(const PdoDecodeResult& result) {
    return result.to_string();
}

/// @brief Serialize a PDO decode result.
void to_json(nlohmann::json& j, const PdoDecodeResult& result);
/// @brief Parse a PDO decode result without throwing.
void from_json(const nlohmann::json& j, PdoDecodeResult& result) noexcept;

/// @brief Canonical name of a PDO decode status.
[[nodiscard]] constexpr std::string_view to_string(PdoDecodeStatus value) noexcept {
    return core::enum_name(value);
}
/// @brief Parse a PDO decode status.
[[nodiscard]] constexpr bool from_string(std::string_view name, PdoDecodeStatus& out) noexcept {
    return core::enum_value(name, out);
}
/// @brief Write a PDO decode status as JSON.
inline void to_json(nlohmann::json& j, const PdoDecodeStatus value) {
    core::mandate::enum_to_json(j, value);
}
/// @brief Read a PDO decode status without throwing.
inline void from_json(const nlohmann::json& j, PdoDecodeStatus& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::canopen
