// nexigen::synchro — generator lifecycle vocabulary
// (change add-synchro-event-sync; spec synchro/protocol).
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::synchro {

/// @brief Lifecycle state of a synchronization generator.
///
/// The numeric values are the payload of the `sync_state` gauge.
///
/// @note Spec `synchro/protocol`, requirement "State vocabularies and endpoint
///       statistics".
enum class SyncState : std::uint8_t {
    Stopped,         ///< No worker owns the generator.
    WaitingForStart, ///< The worker is waiting for the resolved start instant.
    Running,         ///< Phases are being published on the grid.
    Faulted,         ///< The worker stopped because of a clock, eventfd, or thread fault.
};

/// @brief Canonical name of a generator state.
/// @param value State to render.
/// @return Static-storage canonical spelling. Never allocates.
/// @note Spec `synchro/protocol`, requirement "State vocabularies and endpoint
///       statistics"; mandate 9.
[[nodiscard]] constexpr std::string_view to_string(SyncState value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical generator-state name.
/// @param name Candidate spelling.
/// @param out Destination, untouched on a miss.
/// @return `true` on an exact match.
/// @note Spec `synchro/protocol`, requirement "State vocabularies and endpoint
///       statistics".
[[nodiscard]] constexpr bool from_string(std::string_view name, SyncState& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for a generator state.
/// @param j Destination JSON value.
/// @param value State to serialize.
/// @note Mandate 10; cold path only.
inline void to_json(nlohmann::json& j, const SyncState value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for a generator state, strictly non-throwing.
/// @param j Source JSON value.
/// @param value Destination, unchanged on failure.
/// @note Mandate 10; cold path only.
inline void from_json(const nlohmann::json& j, SyncState& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::synchro

namespace nexigen::core {

/// @brief Vocabulary table binding `SyncState` to canonical names.
template <> struct EnumTraits<nexigen::synchro::SyncState> {
    /// @brief One value/name pair.
    struct Entry {
        nexigen::synchro::SyncState value; ///< Enumerator.
        std::string_view name;             ///< Canonical name.
    };
    /// @brief Canonical table in declaration order.
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::synchro::SyncState::Stopped, "Stopped"},
        {nexigen::synchro::SyncState::WaitingForStart, "WaitingForStart"},
        {nexigen::synchro::SyncState::Running, "Running"},
        {nexigen::synchro::SyncState::Faulted, "Faulted"},
    }};
};

} // namespace nexigen::core
