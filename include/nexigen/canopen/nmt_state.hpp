// nexigen::canopen — NMT state vocabulary.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::canopen {

/// @brief NMT state of one CANopen node.
enum class NmtState : std::uint8_t {
    Unknown,        ///< No heartbeat/boot-up observed yet.
    BootUp,         ///< Boot-up message observed.
    Stopped,        ///< NMT Stopped.
    Operational,    ///< NMT Operational.
    PreOperational, ///< NMT Pre-operational.
};

} // namespace nexigen::canopen

namespace nexigen::core {

/// @brief Vocabulary table for `NmtState`.
template <> struct EnumTraits<nexigen::canopen::NmtState> {
    /// @brief One value/name row.
    struct Entry {
        nexigen::canopen::NmtState value; ///< Enumerator.
        std::string_view name;            ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 5> entries{{
        {nexigen::canopen::NmtState::Unknown, "Unknown"},
        {nexigen::canopen::NmtState::BootUp, "BootUp"},
        {nexigen::canopen::NmtState::Stopped, "Stopped"},
        {nexigen::canopen::NmtState::Operational, "Operational"},
        {nexigen::canopen::NmtState::PreOperational, "PreOperational"},
    }};
};

} // namespace nexigen::core

namespace nexigen::canopen {

/// @brief Canonical name of an NMT state.
[[nodiscard]] constexpr std::string_view to_string(NmtState value) noexcept {
    return core::enum_name(value);
}
/// @brief Parse an NMT state.
[[nodiscard]] constexpr bool from_string(std::string_view name, NmtState& out) noexcept {
    return core::enum_value(name, out);
}
/// @brief Write an NMT state as JSON.
inline void to_json(nlohmann::json& j, const NmtState value) {
    core::mandate::enum_to_json(j, value);
}
/// @brief Read an NMT state without throwing.
inline void from_json(const nlohmann::json& j, NmtState& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::canopen
