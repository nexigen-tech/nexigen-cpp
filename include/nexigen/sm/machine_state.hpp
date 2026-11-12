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

/// @brief The main machine's states.
///
/// One machine sequences the whole platform: `Startup` → `Verify` →
/// `NormalRunning`, then `Shutdown` on ignition-off. The arbiter applies each
/// step only after the execution_manager reports the previous one. Values are
/// byte-sized and append-only; the canonical spelling is the enumerator name.
///
/// @note Spec `sm/state-machine`, requirement "Startup and shutdown
///       sequencing".
enum class MachineState : std::uint8_t {
    Startup,       ///< Initial state; the SM sequences from here.
    Verify,        ///< Self-diagnosis processes are running.
    NormalRunning, ///< The platform operates normally.
    Shutdown,      ///< Final state; the EM stops the SM afterwards.
};

/// @brief Canonical name of a machine state.
///
/// @param value Enumerator to render.
/// @return Static-storage view of the enumerator spelling; an out-of-table
///         value renders the engine fallback. Never allocates or throws.
/// @note Spec `sm/state-machine`, requirement "State machines execution".
/// @note Diagnostic string only (project mandate 9).
[[nodiscard]] constexpr std::string_view to_string(MachineState value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical machine-state name.
///
/// @param name View to parse; matching is exact and case-sensitive.
/// @param out Destination, assigned only on success.
/// @return `true` on an exact canonical spelling; `false` otherwise, leaving
///         `out` UNCHANGED.
/// @note Spec `sm/state-machine`, requirement "State machines execution".
[[nodiscard]] constexpr bool from_string(std::string_view name, MachineState& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer: the value is exactly `to_string(state)`.
///
/// @param j Output JSON value replaced with the name string.
/// @param value Enumerator to serialize.
/// @note Cold-path JSON only (project mandate 10).
inline void to_json(nlohmann::json& j, const MachineState value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader, strictly non-throwing under `JSON_NOEXCEPTION=1`.
///
/// A non-string or unrecognized value leaves the destination unchanged.
///
/// @param j JSON value to read.
/// @param out Destination, unchanged on any failure.
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
inline void from_json(const nlohmann::json& j, MachineState& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::sm

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

/// @brief Vocabulary table mapping `MachineState` enumerators to their names.
///
/// Backs `core::enum_name`/`enum_value`, hence `to_string`, `from_string`, and
/// the JSON adapters. The table is `constexpr` static storage and lookup is a
/// linear scan over four rows, so no allocation occurs.
///
/// @note Spec `sm/state-machine`, requirement "State machines execution".
template <> struct EnumTraits<nexigen::sm::MachineState> {
    /// @brief One enumerator/name pair of the vocabulary table.
    struct Entry {
        nexigen::sm::MachineState value; ///< Enumerator being named.
        std::string_view name;           ///< Canonical PascalCase spelling.
    };
    /// @brief Canonical enumerator table, in declaration order.
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::sm::MachineState::Startup, "Startup"},
        {nexigen::sm::MachineState::Verify, "Verify"},
        {nexigen::sm::MachineState::NormalRunning, "NormalRunning"},
        {nexigen::sm::MachineState::Shutdown, "Shutdown"},
    }};
};

} // namespace nexigen::core
