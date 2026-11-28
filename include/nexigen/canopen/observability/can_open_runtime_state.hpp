// nexigen::canopen::observability — runtime lifecycle vocabulary (change
// add-canopen-observability; spec canopen/observability).
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::canopen::observability {

/// @brief Lifecycle state reported by a CANopen bus runtime.
///
/// The enumerator spelling is also the value stored in the `canopen_state`
/// state gauge, so operators read `Running` rather than a numeric code.
///
/// @note Spec `canopen/observability`, requirement "Tier 1 loop metrics".
enum class CanOpenRuntimeState : std::uint8_t {
    Stopped,  ///< No runtime thread owns the bus.
    Starting, ///< Startup validation and transport opening are in progress.
    Running,  ///< Cycles are executing on the bus.
    Degraded, ///< The runtime is alive but at least one health check failed.
    Error,    ///< The runtime stopped because of an unrecoverable fault.
};

} // namespace nexigen::canopen::observability

namespace nexigen::core {

/// @brief Vocabulary table binding `CanOpenRuntimeState` to canonical names.
template <> struct EnumTraits<nexigen::canopen::observability::CanOpenRuntimeState> {
    /// @brief One enumerator/name pair.
    struct Entry {
        nexigen::canopen::observability::CanOpenRuntimeState value; ///< Enumerator.
        std::string_view name;                                      ///< Canonical name.
    };
    /// @brief Canonical table in declaration order.
    static constexpr std::array<Entry, 5> entries{{
        {nexigen::canopen::observability::CanOpenRuntimeState::Stopped, "Stopped"},
        {nexigen::canopen::observability::CanOpenRuntimeState::Starting, "Starting"},
        {nexigen::canopen::observability::CanOpenRuntimeState::Running, "Running"},
        {nexigen::canopen::observability::CanOpenRuntimeState::Degraded, "Degraded"},
        {nexigen::canopen::observability::CanOpenRuntimeState::Error, "Error"},
    }};
};

} // namespace nexigen::core

namespace nexigen::canopen::observability {

/// @brief Canonical name of a runtime state.
///
/// @param value State to render.
/// @return Static-storage canonical spelling; never allocates.
/// @note Spec `canopen/observability`, requirement "Observability vocabulary is
///       serializable"; mandate 9.
[[nodiscard]] constexpr std::string_view to_string(CanOpenRuntimeState value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical runtime-state name.
///
/// @param name Candidate spelling; case-sensitive.
/// @param out Destination, untouched on a miss.
/// @return `true` on an exact match.
/// @note Spec `canopen/observability`, requirement "Observability vocabulary is
///       serializable".
[[nodiscard]] constexpr bool from_string(std::string_view name, CanOpenRuntimeState& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for a runtime state.
///
/// @param j Destination JSON value.
/// @param value State to serialize.
/// @note Mandate 10; cold path only.
inline void to_json(nlohmann::json& j, const CanOpenRuntimeState value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for a runtime state, strictly non-throwing.
///
/// @param j Source JSON value.
/// @param value Destination, unchanged on a non-string or unknown name.
/// @note Mandate 10; cold path only.
inline void from_json(const nlohmann::json& j, CanOpenRuntimeState& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::canopen::observability
