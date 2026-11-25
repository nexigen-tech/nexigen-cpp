// nexigen-canview — CANopen vocabulary and CiA 301 data types.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::canview {

/// @brief NMT state of one bound node.
enum class NmtState : std::uint8_t {
    Unknown,        ///< No heartbeat/boot-up observed yet.
    BootUp,         ///< Boot-up message observed.
    Stopped,        ///< NMT Stopped.
    Operational,    ///< NMT Operational.
    PreOperational, ///< NMT Pre-operational.
};

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

/// @brief EDS access type.
enum class EdsAccess : std::uint8_t {
    Unknown,         ///< Missing or unrecognised.
    ReadOnly,        ///< `ro`.
    WriteOnly,       ///< `wo`.
    ReadWrite,       ///< `rw`.
    ReadWriteInput,  ///< `rwr`.
    ReadWriteOutput, ///< `rww`.
    Constant,        ///< `const`.
};

} // namespace nexigen::canview

namespace nexigen::core {

/// @brief Vocabulary table for `NmtState`.
template <> struct EnumTraits<nexigen::canview::NmtState> {
    /// @brief One value/name row.

    struct Entry {
        nexigen::canview::NmtState value; ///< Enumerator.
        std::string_view name;            ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 5> entries{{
        {nexigen::canview::NmtState::Unknown, "Unknown"},
        {nexigen::canview::NmtState::BootUp, "BootUp"},
        {nexigen::canview::NmtState::Stopped, "Stopped"},
        {nexigen::canview::NmtState::Operational, "Operational"},
        {nexigen::canview::NmtState::PreOperational, "PreOperational"},
    }};
};

/// @brief Vocabulary table for `CoService`.
template <> struct EnumTraits<nexigen::canview::CoService> {
    /// @brief One value/name row.

    struct Entry {
        nexigen::canview::CoService value; ///< Enumerator.
        std::string_view name;             ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 10> entries{{
        {nexigen::canview::CoService::Unknown, "Unknown"},
        {nexigen::canview::CoService::Nmt, "Nmt"},
        {nexigen::canview::CoService::Sync, "Sync"},
        {nexigen::canview::CoService::Time, "Time"},
        {nexigen::canview::CoService::Emcy, "Emcy"},
        {nexigen::canview::CoService::SdoRequest, "SdoRequest"},
        {nexigen::canview::CoService::SdoResponse, "SdoResponse"},
        {nexigen::canview::CoService::Tpdo, "Tpdo"},
        {nexigen::canview::CoService::Rpdo, "Rpdo"},
        {nexigen::canview::CoService::Heartbeat, "Heartbeat"},
    }};
};

/// @brief Vocabulary table for `EdsAccess`.
template <> struct EnumTraits<nexigen::canview::EdsAccess> {
    /// @brief One value/name row.

    struct Entry {
        nexigen::canview::EdsAccess value; ///< Enumerator.
        std::string_view name;             ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 7> entries{{
        {nexigen::canview::EdsAccess::Unknown, "Unknown"},
        {nexigen::canview::EdsAccess::ReadOnly, "ReadOnly"},
        {nexigen::canview::EdsAccess::WriteOnly, "WriteOnly"},
        {nexigen::canview::EdsAccess::ReadWrite, "ReadWrite"},
        {nexigen::canview::EdsAccess::ReadWriteInput, "ReadWriteInput"},
        {nexigen::canview::EdsAccess::ReadWriteOutput, "ReadWriteOutput"},
        {nexigen::canview::EdsAccess::Constant, "Constant"},
    }};
};

} // namespace nexigen::core

namespace nexigen::canview {

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

/// @brief Canonical name of an EDS access type.
[[nodiscard]] constexpr std::string_view to_string(EdsAccess value) noexcept {
    return core::enum_name(value);
}
/// @brief Parse an EDS access type.
[[nodiscard]] constexpr bool from_string(std::string_view name, EdsAccess& out) noexcept {
    return core::enum_value(name, out);
}
/// @brief Write an EDS access type as JSON.
inline void to_json(nlohmann::json& j, const EdsAccess value) {
    core::mandate::enum_to_json(j, value);
}
/// @brief Read an EDS access type without throwing.
inline void from_json(const nlohmann::json& j, EdsAccess& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

/// @brief Kind of a CiA 301 data type.
enum class EdsDataKind : std::uint8_t {
    Boolean,
    Integer,
    Unsigned,
    Real,
    String,
    Time,
    Domain,
    Unknown,
};

} // namespace nexigen::canview

namespace nexigen::core {

/// @brief Vocabulary table for `EdsDataKind`.
template <> struct EnumTraits<nexigen::canview::EdsDataKind> {
    struct Entry {
        nexigen::canview::EdsDataKind value; ///< Enumerator.
        std::string_view name;               ///< Canonical name.
    };
    static constexpr std::array<Entry, 8> entries{{
        {nexigen::canview::EdsDataKind::Boolean, "Boolean"},
        {nexigen::canview::EdsDataKind::Integer, "Integer"},
        {nexigen::canview::EdsDataKind::Unsigned, "Unsigned"},
        {nexigen::canview::EdsDataKind::Real, "Real"},
        {nexigen::canview::EdsDataKind::String, "String"},
        {nexigen::canview::EdsDataKind::Time, "Time"},
        {nexigen::canview::EdsDataKind::Domain, "Domain"},
        {nexigen::canview::EdsDataKind::Unknown, "Unknown"},
    }};
};

} // namespace nexigen::core

namespace nexigen::canview {

/// @brief One CiA 301 data type descriptor.
struct EdsDataTypeInfo {
    std::uint16_t code{0};                  ///< EDS data type code.
    std::string_view name{};                ///< Canonical name.
    std::uint8_t bits{0};                   ///< Bit width; zero for variable-length strings.
    EdsDataKind kind{EdsDataKind::Unknown}; ///< Interpretation.
    bool signed_value{false};               ///< Signed integer.
};

/// @brief Resolve a CiA 301 data type code.
/// @param code EDS data type code.
/// @return Descriptor; an unknown code returns the `Unknown` entry.
[[nodiscard]] const EdsDataTypeInfo& eds_data_type(std::uint16_t code) noexcept;

/// @brief Parse an EDS access keyword.
/// @param text Keyword, case-insensitive.
/// @return Access type.
[[nodiscard]] EdsAccess parse_eds_access(std::string_view text) noexcept;

/// @brief Canonical name of a data kind.
[[nodiscard]] constexpr std::string_view to_string(EdsDataKind value) noexcept {
    return core::enum_name(value);
}
/// @brief Parse a data kind.
[[nodiscard]] constexpr bool from_string(std::string_view name, EdsDataKind& out) noexcept {
    return core::enum_value(name, out);
}
/// @brief Write a data kind as JSON.
inline void to_json(nlohmann::json& j, const EdsDataKind value) {
    core::mandate::enum_to_json(j, value);
}
/// @brief Read a data kind without throwing.
inline void from_json(const nlohmann::json& j, EdsDataKind& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::canview
