// nexigen::phm — watchdog state vocabulary (change close-phm-safety-loop).
//
// The observable state of the daemon-owned watchdog. `Disabled` is the
// manifest default; `Armed` means the device is open and being fed; `Stopped`
// means feeding was deliberately stopped (an unrecoverable supervision state or
// a lost device), which lets the hardware reset the machine.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::phm {

/// @brief Whether the daemon is feeding its watchdog.
enum class WatchdogState : std::uint8_t {
    Disabled, ///< No device is opened; supervision runs without a watchdog.
    Armed,    ///< The device is open and healthy cycles feed it.
    Stopped,  ///< Feeding was stopped; the hardware timeout will reset the machine.
};

/// @brief Canonical name of a watchdog state.
///
/// @param value State to render; an out-of-range cast renders `Unknown`.
/// @return Static-storage view of the enumerator spelling.
/// @note Mandate 9; allocation-free and `constexpr`. Spec `phm/watchdog`,
///       requirement "Watchdog diagnostics".
[[nodiscard]] constexpr std::string_view to_string(WatchdogState value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical watchdog state name.
///
/// @param name Candidate canonical name, as produced by `to_string`.
/// @param out Receives the parsed value; left UNCHANGED when parsing fails.
/// @return `true` when `name` matched an enumerator, `false` otherwise.
/// @note Mandate 9; allocation-free and `constexpr`.
[[nodiscard]] constexpr bool from_string(std::string_view name, WatchdogState& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for a watchdog state.
///
/// The encoded value is exactly `to_string(value)`.
///
/// @param j Destination JSON value; overwritten.
/// @param value State to encode.
/// @note Mandate 10; cold path only.
inline void to_json(nlohmann::json& j, const WatchdogState value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for a watchdog state (non-throwing).
///
/// @param j Source JSON value.
/// @param out Receives the parsed value; left UNCHANGED on failure.
/// @note Mandate 10; validated under `JSON_NOEXCEPTION=1`.
inline void from_json(const nlohmann::json& j, WatchdogState& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::phm

namespace nexigen::core {

/// @brief `WatchdogState` vocabulary table.
template <> struct EnumTraits<nexigen::phm::WatchdogState> {
    /// @brief One value/name pair of the vocabulary table.
    struct Entry {
        nexigen::phm::WatchdogState value; ///< Enumerator.
        std::string_view name;             ///< Canonical enumerator name.
    };
    /// @brief Canonical table; exactly one entry per enumerator.
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::phm::WatchdogState::Disabled, "Disabled"},
        {nexigen::phm::WatchdogState::Armed, "Armed"},
        {nexigen::phm::WatchdogState::Stopped, "Stopped"},
    }};
};

} // namespace nexigen::core

/// @brief `std::formatter` for a watchdog state (project mandate 11).
template <> struct std::formatter<nexigen::phm::WatchdogState> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::phm::WatchdogState value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::phm::to_string(value), ctx);
    }
};
