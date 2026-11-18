// nexigen::synchro — start mode vocabulary
// (change add-synchro-event-sync; spec synchro/protocol).
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::synchro {

/// @brief How a generator resolves its first cycle.
///
/// `NextCycle` ignores any target and starts on the next grid boundary strictly
/// after the call. `AtTarget` honours the configured or runtime target, subject
/// to alignment and the past-target policy.
///
/// @note Spec `synchro/protocol`, requirement "Start policy".
enum class StartMode : std::uint8_t {
    NextCycle, ///< Start on the next cycle boundary after "now".
    AtTarget,  ///< Start at the configured or runtime target instant.
};

/// @brief Canonical name of a start mode.
/// @param value Mode to render.
/// @return Static-storage canonical spelling; out-of-table values render the
///         engine fallback. Never allocates.
/// @note Spec `synchro/protocol`, requirement "Start policy"; mandate 9.
[[nodiscard]] constexpr std::string_view to_string(StartMode value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical start-mode name.
/// @param name Candidate spelling.
/// @param out Destination, untouched on a miss.
/// @return `true` on an exact match.
/// @note Spec `synchro/protocol`, requirement "Start policy".
[[nodiscard]] constexpr bool from_string(std::string_view name, StartMode& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for a start mode.
/// @param j Destination JSON value.
/// @param value Mode to serialize.
/// @note Mandate 10; cold path only.
inline void to_json(nlohmann::json& j, const StartMode value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for a start mode, strictly non-throwing.
/// @param j Source JSON value; a non-string or unknown name leaves `value`.
/// @param value Destination, unchanged on failure.
/// @note Mandate 10; cold path only.
inline void from_json(const nlohmann::json& j, StartMode& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::synchro

namespace nexigen::core {

/// @brief Vocabulary table binding `StartMode` to canonical names.
template <> struct EnumTraits<nexigen::synchro::StartMode> {
    /// @brief One value/name pair.
    struct Entry {
        nexigen::synchro::StartMode value; ///< Enumerator.
        std::string_view name;             ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::synchro::StartMode::NextCycle, "NextCycle"},
        {nexigen::synchro::StartMode::AtTarget, "AtTarget"},
    }};
};

} // namespace nexigen::core
