// nexigen::sm — main machine state (change add-sm; spec sm/state-machine).
//
// The fixed states of the platform's one MachineState machine. Own enum with
// the full project conversion set.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::sm {

/// The main machine's states.
enum class MachineState : std::uint8_t {
    Startup,       ///< Initial state; the SM sequences from here.
    Verify,        ///< Self-diagnosis processes are running.
    NormalRunning, ///< The platform operates normally.
    Shutdown,      ///< Final state; the EM stops the SM afterwards.
};

/// Canonical name of a machine state; allocation-free, constexpr.
[[nodiscard]] constexpr std::string_view to_string(MachineState value) noexcept {
    return core::enum_name(value);
}

/// Parse a canonical machine-state name; false leaves the out-parameter UNCHANGED.
[[nodiscard]] constexpr bool from_string(std::string_view name, MachineState& out) noexcept {
    return core::enum_value(name, out);
}

/// ADL JSON writer (cold path only): the value is exactly to_string(state).
inline void to_json(nlohmann::json& j, const MachineState value) {
    core::mandate::enum_to_json(j, value);
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1.
inline void from_json(const nlohmann::json& j, MachineState& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::sm

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::sm::MachineState> {
    struct Entry {
        nexigen::sm::MachineState value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::sm::MachineState::Startup, "Startup"},
        {nexigen::sm::MachineState::Verify, "Verify"},
        {nexigen::sm::MachineState::NormalRunning, "NormalRunning"},
        {nexigen::sm::MachineState::Shutdown, "Shutdown"},
    }};
};

} // namespace nexigen::core
