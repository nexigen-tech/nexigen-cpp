// nexigen::phm — recovery action vocabulary (change add-phm; spec phm/recovery).
//
// The action the daemon takes when a process's supervision status becomes
// Expired. Own enum with the full project conversion set.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::phm {

/// Recovery strategy attached to an Expired supervision status.
enum class RecoveryAction : std::uint8_t {
    RestartProcess,           ///< Ask the EM to restart the failed process.
    SwitchFunctionGroupState, ///< Escalate: move the function group to SafeState.
    TriggerWatchdogReset,     ///< Escalate: hard-reset via the hardware watchdog.
};

/// Canonical name of a recovery action; allocation-free, constexpr.
[[nodiscard]] constexpr std::string_view to_string(RecoveryAction value) noexcept {
    return core::enum_name(value);
}

/// Parse a canonical recovery-action name; false leaves the out-parameter UNCHANGED.
[[nodiscard]] constexpr bool from_string(std::string_view name, RecoveryAction& out) noexcept {
    return core::enum_value(name, out);
}

/// ADL JSON writer (cold path only): the value is exactly to_string(action).
inline void to_json(nlohmann::json& j, const RecoveryAction value) {
    core::mandate::enum_to_json(j, value);
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1.
inline void from_json(const nlohmann::json& j, RecoveryAction& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::phm

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::phm::RecoveryAction> {
    struct Entry {
        nexigen::phm::RecoveryAction value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::phm::RecoveryAction::RestartProcess, "RestartProcess"},
        {nexigen::phm::RecoveryAction::SwitchFunctionGroupState, "SwitchFunctionGroupState"},
        {nexigen::phm::RecoveryAction::TriggerWatchdogReset, "TriggerWatchdogReset"},
    }};
};

} // namespace nexigen::core
