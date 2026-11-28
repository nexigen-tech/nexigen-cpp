// nexigen::canopen::observability — error-domain vocabulary (change
// add-canopen-observability; spec canopen/observability).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::canopen::observability {

/// @brief Subsystem that produced one hot-path CANopen error.
///
/// The nine domains form the second dimension of the fixed error gate: one
/// episode slot per node and domain. The enumerator values are contiguous from
/// zero so the gate can index its storage directly.
///
/// @note Spec `canopen/observability`, requirement "Error tracking is
///       fixed-size"; design D5.
enum class CanOpenErrorDomain : std::uint8_t {
    Bus,       ///< Bus state, bus-off, and controller-level faults.
    Transport, ///< Driver, queue, and link faults.
    Nmt,       ///< Network management and heartbeat supervision.
    Sdo,       ///< Service data object transfers.
    Pdo,       ///< Process data object mapping and decoding.
    Emcy,      ///< Emergency objects.
    Signal,    ///< Signal conversion, staleness, and range checks.
    Ipc,       ///< Publish/subscribe boundary faults owned by the runtime edge.
    Config,    ///< Configuration, EDS/DCF loading, and startup validation.
};

/// @brief Number of `CanOpenErrorDomain` enumerators.
///
/// The gate allocates exactly one episode row per domain per node.
///
/// @note Spec `canopen/observability`, requirement "Error tracking is
///       fixed-size".
inline constexpr std::size_t error_domain_count = 9U;

} // namespace nexigen::canopen::observability

namespace nexigen::core {

/// @brief Vocabulary table binding `CanOpenErrorDomain` to lowercase names.
template <> struct EnumTraits<nexigen::canopen::observability::CanOpenErrorDomain> {
    /// @brief One enumerator/name pair.
    struct Entry {
        nexigen::canopen::observability::CanOpenErrorDomain value; ///< Enumerator.
        std::string_view name;                                     ///< Canonical lowercase name.
    };
    /// @brief Canonical table in declaration order.
    static constexpr std::array<Entry, 9> entries{{
        {nexigen::canopen::observability::CanOpenErrorDomain::Bus, "bus"},
        {nexigen::canopen::observability::CanOpenErrorDomain::Transport, "transport"},
        {nexigen::canopen::observability::CanOpenErrorDomain::Nmt, "nmt"},
        {nexigen::canopen::observability::CanOpenErrorDomain::Sdo, "sdo"},
        {nexigen::canopen::observability::CanOpenErrorDomain::Pdo, "pdo"},
        {nexigen::canopen::observability::CanOpenErrorDomain::Emcy, "emcy"},
        {nexigen::canopen::observability::CanOpenErrorDomain::Signal, "signal"},
        {nexigen::canopen::observability::CanOpenErrorDomain::Ipc, "ipc"},
        {nexigen::canopen::observability::CanOpenErrorDomain::Config, "config"},
    }};
};

} // namespace nexigen::core

namespace nexigen::canopen::observability {

/// @brief Canonical lowercase name of an error domain.
///
/// @param value Domain to render.
/// @return Static-storage canonical spelling; never allocates.
/// @note Spec `canopen/observability`, requirement "Observability vocabulary is
///       serializable"; mandate 9.
[[nodiscard]] constexpr std::string_view to_string(CanOpenErrorDomain value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical lowercase error-domain name.
///
/// @param name Candidate spelling; case-sensitive.
/// @param out Destination, untouched on a miss.
/// @return `true` on an exact match.
/// @note Spec `canopen/observability`, requirement "Observability vocabulary is
///       serializable".
[[nodiscard]] constexpr bool from_string(std::string_view name, CanOpenErrorDomain& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for an error domain.
///
/// @param j Destination JSON value.
/// @param value Domain to serialize.
/// @note Mandate 10; cold path only.
inline void to_json(nlohmann::json& j, const CanOpenErrorDomain value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for an error domain, strictly non-throwing.
///
/// @param j Source JSON value.
/// @param value Destination, unchanged on a non-string or unknown name.
/// @note Mandate 10; cold path only.
inline void from_json(const nlohmann::json& j, CanOpenErrorDomain& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::canopen::observability
