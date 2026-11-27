// nexigen::canopen — CANopen service vocabulary.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::canopen {

/// @brief CANopen service decoded from a COB-ID.
enum class CoService : std::uint8_t {
    Unknown,     ///< No route.
    Nmt,         ///< Network management.
    Sync,        ///< SYNC.
    Time,        ///< TIME.
    Emcy,        ///< Emergency.
    SdoRequest,  ///< SDO request (client -> server).
    SdoResponse, ///< SDO response (server -> client).
    Tpdo,        ///< Transmit PDO.
    Rpdo,        ///< Receive PDO.
    Heartbeat,   ///< Heartbeat / boot-up.
};

} // namespace nexigen::canopen

namespace nexigen::core {

/// @brief Vocabulary table for `CoService`.
template <> struct EnumTraits<nexigen::canopen::CoService> {
    /// @brief One value/name row.
    struct Entry {
        nexigen::canopen::CoService value; ///< Enumerator.
        std::string_view name;             ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 10> entries{{
        {nexigen::canopen::CoService::Unknown, "Unknown"},
        {nexigen::canopen::CoService::Nmt, "Nmt"},
        {nexigen::canopen::CoService::Sync, "Sync"},
        {nexigen::canopen::CoService::Time, "Time"},
        {nexigen::canopen::CoService::Emcy, "Emcy"},
        {nexigen::canopen::CoService::SdoRequest, "SdoRequest"},
        {nexigen::canopen::CoService::SdoResponse, "SdoResponse"},
        {nexigen::canopen::CoService::Tpdo, "Tpdo"},
        {nexigen::canopen::CoService::Rpdo, "Rpdo"},
        {nexigen::canopen::CoService::Heartbeat, "Heartbeat"},
    }};
};

} // namespace nexigen::core

namespace nexigen::canopen {

/// @brief Canonical name of a CANopen service.
[[nodiscard]] constexpr std::string_view to_string(CoService value) noexcept {
    return core::enum_name(value);
}
/// @brief Parse a CANopen service.
[[nodiscard]] constexpr bool from_string(std::string_view name, CoService& out) noexcept {
    return core::enum_value(name, out);
}
/// @brief Write a CANopen service as JSON.
inline void to_json(nlohmann::json& j, const CoService value) {
    core::mandate::enum_to_json(j, value);
}
/// @brief Read a CANopen service without throwing.
inline void from_json(const nlohmann::json& j, CoService& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::canopen
