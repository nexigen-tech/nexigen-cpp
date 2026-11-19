// nexigen::chrono — time-base domain lock vocabulary (change
// add-time-base-service; spec chrono/time-base "Domain coefficient row").
//
// The lock state is the per-domain truth behind the aggregated time-base
// status: `Locked` means the map is measured and current, `Holdover` means it
// is extrapolated from the last measurement, `FreeRun` means the domain is
// running on its own counter without a measured relation, and `Unsupported`
// means the hardware cannot be related to the canonical scale at all.
//
// `FreeRun` and `Unsupported` are normal states, not errors: a hardware module
// without PTM is expected. Cold path only.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::chrono {

/// @brief Lock state of one time domain.
///
/// @note Spec `chrono/time-base`, requirement "Domain coefficient row".
enum class LockState : std::uint8_t {
    Locked,      ///< The map is measured and current.
    Holdover,    ///< The map is extrapolated from the last measurement.
    FreeRun,     ///< The domain runs on its own counter; no live relation.
    Unsupported, ///< The domain cannot be related to the canonical scale.
};

} // namespace nexigen::chrono

// Vocabulary table for core::enum_name/enum_value (mandates 9-11).
namespace nexigen::core {

/// @brief Vocabulary table wiring `LockState` into the shared enum engine.
///
/// Canonical wire spellings are `free_run` and `unsupported`; both are normal
/// states of a deployed platform.
///
/// @note Spec `chrono/time-base`, requirement "Domain coefficient row"; project
///       mandates 9 and 10.
template <> struct EnumTraits<nexigen::chrono::LockState> {
    /// @brief One lock-state-to-name row.
    struct Entry {
        nexigen::chrono::LockState value; ///< Enumerator described by this row.
        std::string_view name;            ///< Canonical wire spelling.
    };
    /// @brief Append-only rows in enumerator order.
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::chrono::LockState::Locked, "locked"},
        {nexigen::chrono::LockState::Holdover, "holdover"},
        {nexigen::chrono::LockState::FreeRun, "free_run"},
        {nexigen::chrono::LockState::Unsupported, "unsupported"},
    }};
};

} // namespace nexigen::core

namespace nexigen::chrono {

/// @brief Canonical name of a lock state.
///
/// @param value State to name.
/// @return Static-storage view of the canonical spelling; out-of-table values
///         render `"Unknown"`.
/// @note Spec `chrono/time-base`, requirement "Domain coefficient row";
///       mandate 9.
[[nodiscard]] constexpr std::string_view to_string(LockState value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical lock state name.
///
/// @param name Candidate canonical spelling.
/// @param out Destination enumerator, untouched on a miss.
/// @return `true` when `name` matched a row.
/// @note Spec `chrono/time-base`, requirement "Domain coefficient row";
///       mandate 9.
[[nodiscard]] constexpr bool from_string(std::string_view name, LockState& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for `LockState` (cold path only).
///
/// @param j Destination JSON value.
/// @param value State to serialize.
/// @note Spec `chrono/time-base`, requirement "Domain coefficient row";
///       mandate 10.
inline void to_json(nlohmann::json& j, const LockState value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for `LockState`, strictly non-throwing.
///
/// @param j Source JSON value.
/// @param out Destination enumerator, untouched unless a known name parses.
/// @note Spec `chrono/time-base`, requirement "Domain coefficient row";
///       mandate 10.
inline void from_json(const nlohmann::json& j, LockState& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::chrono
