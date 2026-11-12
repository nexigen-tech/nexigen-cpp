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

/// @brief Recovery strategy attached to an `Expired` supervision status.
///
/// A plain `std::uint8_t` enum with the project conversion set: canonical
/// `to_string`/`from_string`, ADL `to_json`/`from_json`, and the shared generic
/// `std::formatter`. `RestartProcess` is local recovery over the PHM->EM
/// request-response channel; the other two escalate.
///
/// @note Spec `phm/recovery`, requirement "Recovery action vocabulary".
///       Mandates 9/10/11 cover the string, JSON, and formatter surfaces.
enum class RecoveryAction : std::uint8_t {
    RestartProcess,           ///< Ask the EM to restart the failed process.
    SwitchFunctionGroupState, ///< Escalate: move the function group to SafeState.
    TriggerWatchdogReset,     ///< Escalate: hard-reset via the hardware watchdog.
};

/// @brief Canonical name of a recovery action.
///
/// @param value Action to render; an out-of-range cast renders `Unknown`.
/// @return The canonical enumerator name.
/// @note Mandate 9 (universal `to_string`); allocation-free and `constexpr`.
///       Spec `phm/recovery`, requirement "Recovery action vocabulary".
[[nodiscard]] constexpr std::string_view to_string(RecoveryAction value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical recovery-action name.
///
/// @param name Candidate canonical name, as produced by `to_string`.
/// @param out Receives the parsed value; left UNCHANGED when parsing fails.
/// @return `true` when `name` matched an enumerator, `false` otherwise.
/// @note Mandate 9; allocation-free and `constexpr`. Spec `phm/recovery`,
///       requirement "Recovery action vocabulary".
[[nodiscard]] constexpr bool from_string(std::string_view name, RecoveryAction& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for a recovery action.
///
/// The encoded value is exactly `to_string(value)`.
///
/// @param j Destination JSON value; overwritten.
/// @param value Action to encode.
/// @note Mandate 10 (ADL JSON pair); cold path only. Spec `phm/recovery`,
///       requirement "Recovery action vocabulary".
inline void to_json(nlohmann::json& j, const RecoveryAction value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for a recovery action.
///
/// Strictly non-throwing: a malformed or unknown name leaves `out` unchanged.
///
/// @param j Source JSON value.
/// @param out Receives the parsed value; left UNCHANGED on failure.
/// @note Mandate 10; validated under `JSON_NOEXCEPTION=1`. Spec
///       `phm/recovery`, requirement "Recovery action vocabulary".
inline void from_json(const nlohmann::json& j, RecoveryAction& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::phm

namespace nexigen::core {

/// @brief Vocabulary table binding `phm::RecoveryAction` to its canonical names.
///
/// Consumed by `core::enum_name`/`core::enum_value`; entry order is the
/// canonical round-trip order.
///
/// @note Mandate 9. Spec `phm/recovery`, requirement "Recovery action
///       vocabulary".
template <> struct EnumTraits<nexigen::phm::RecoveryAction> {
    /// @brief One value/name pair of the vocabulary table.
    struct Entry {
        nexigen::phm::RecoveryAction value; ///< Enumerator.
        std::string_view name;              ///< Canonical enumerator name.
    };
    /// @brief Canonical table; exactly one entry per enumerator.
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::phm::RecoveryAction::RestartProcess, "RestartProcess"},
        {nexigen::phm::RecoveryAction::SwitchFunctionGroupState, "SwitchFunctionGroupState"},
        {nexigen::phm::RecoveryAction::TriggerWatchdogReset, "TriggerWatchdogReset"},
    }};
};

} // namespace nexigen::core
