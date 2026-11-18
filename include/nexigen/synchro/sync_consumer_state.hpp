// nexigen::synchro — consumer lock vocabulary
// (change add-synchro-event-sync; spec synchro/protocol).
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::synchro {

/// @brief Whether a consumer currently holds synchronization with a producer.
///
/// @note Spec `synchro/protocol`, requirement "State vocabularies and endpoint
///       statistics".
enum class SyncConsumerState : std::uint8_t {
    Unsynced, ///< No valid event has arrived, or the watchdog expired.
    Locked,   ///< The last accepted event arrived inside the watchdog window.
};

/// @brief Canonical name of a consumer state.
/// @param value State to render.
/// @return Static-storage canonical spelling. Never allocates.
/// @note Spec `synchro/protocol`, requirement "State vocabularies and endpoint
///       statistics"; mandate 9.
[[nodiscard]] constexpr std::string_view to_string(SyncConsumerState value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical consumer-state name.
/// @param name Candidate spelling.
/// @param out Destination, untouched on a miss.
/// @return `true` on an exact match.
/// @note Spec `synchro/protocol`, requirement "State vocabularies and endpoint
///       statistics".
[[nodiscard]] constexpr bool from_string(std::string_view name, SyncConsumerState& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for a consumer state.
/// @param j Destination JSON value.
/// @param value State to serialize.
/// @note Mandate 10; cold path only.
inline void to_json(nlohmann::json& j, const SyncConsumerState value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for a consumer state, strictly non-throwing.
/// @param j Source JSON value.
/// @param value Destination, unchanged on failure.
/// @note Mandate 10; cold path only.
inline void from_json(const nlohmann::json& j, SyncConsumerState& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::synchro

namespace nexigen::core {

/// @brief Vocabulary table binding `SyncConsumerState` to canonical names.
template <> struct EnumTraits<nexigen::synchro::SyncConsumerState> {
    /// @brief One value/name pair.
    struct Entry {
        nexigen::synchro::SyncConsumerState value; ///< Enumerator.
        std::string_view name;                     ///< Canonical name.
    };
    /// @brief Canonical table in declaration order.
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::synchro::SyncConsumerState::Unsynced, "Unsynced"},
        {nexigen::synchro::SyncConsumerState::Locked, "Locked"},
    }};
};

} // namespace nexigen::core
