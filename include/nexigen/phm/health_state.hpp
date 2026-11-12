// nexigen::phm — health-channel state (change add-phm; spec phm/health-channel).
//
// The state a supervised process reports on its health channel. Own enum so
// business code never names an iox2 type; carries the full project conversion
// set (mandates 9-11).
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::phm {

/// @brief Health state of one supervised channel, worst-first semantics.
///
/// The value an application publishes on its health channel; it travels as the
/// iceoryx2 event id, so the daemon receives the transition with no socket and
/// no serialization step. The ordering is deliberate: `Ok` is the nominal
/// state and `Critical` the most severe, so comparisons express "at least as
/// bad as".
///
/// @note Spec `phm/health-channel`, requirement "Health channel states": the
///       enum exposes exactly `Ok`, `Warning`, `Error`, and `Critical` with
///       the full conversion set. Mandates 9/10/11 cover `to_string`, the ADL
///       JSON pair, and the generic `std::formatter`.
enum class HealthState : std::uint8_t {
    Ok,       ///< Nominal operation.
    Warning,  ///< Degraded but tolerable.
    Error,    ///< A fault was detected.
    Critical, ///< Safety-relevant fault.
};

/// @brief Canonical name of a health state.
///
/// @param value Health state to render; an out-of-range cast renders `Unknown`.
/// @return The canonical enumerator name (`Ok`, `Warning`, `Error`, or
///         `Critical`).
/// @note Mandate 9 (universal `to_string`); allocation-free and `constexpr`.
///       Spec `phm/health-channel`, requirement "Health channel states".
[[nodiscard]] constexpr std::string_view to_string(HealthState value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical health-state name.
///
/// @param name Candidate canonical name, as produced by `to_string`.
/// @param out Receives the parsed value; left UNCHANGED when parsing fails.
/// @return `true` when `name` matched an enumerator, `false` otherwise.
/// @note Mandate 9; allocation-free and `constexpr`. Spec `phm/health-channel`,
///       requirement "Health channel states".
[[nodiscard]] constexpr bool from_string(std::string_view name, HealthState& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for a health state.
///
/// The encoded value is exactly `to_string(value)`.
///
/// @param j Destination JSON value; overwritten.
/// @param value Health state to encode.
/// @note Mandate 10 (ADL JSON pair); cold path only. Spec `phm/health-channel`,
///       requirement "Health channel states".
inline void to_json(nlohmann::json& j, const HealthState value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for a health state.
///
/// Strictly non-throwing: a malformed or unknown name leaves `out` unchanged.
///
/// @param j Source JSON value.
/// @param out Receives the parsed value; left UNCHANGED on failure.
/// @note Mandate 10; validated under `JSON_NOEXCEPTION=1`. Spec
///       `phm/health-channel`, requirement "Health channel states".
inline void from_json(const nlohmann::json& j, HealthState& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::phm

namespace nexigen::core {

/// @brief Vocabulary table binding `phm::HealthState` to its canonical names.
///
/// Consumed by `core::enum_name`/`core::enum_value`; entry order is the
/// canonical round-trip order.
///
/// @note Mandate 9. Spec `phm/health-channel`, requirement "Health channel
///       states".
template <> struct EnumTraits<nexigen::phm::HealthState> {
    /// @brief One value/name pair of the vocabulary table.
    struct Entry {
        nexigen::phm::HealthState value; ///< Enumerator.
        std::string_view name;           ///< Canonical enumerator name.
    };
    /// @brief Canonical table; exactly one entry per enumerator.
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::phm::HealthState::Ok, "Ok"},
        {nexigen::phm::HealthState::Warning, "Warning"},
        {nexigen::phm::HealthState::Error, "Error"},
        {nexigen::phm::HealthState::Critical, "Critical"},
    }};
};

} // namespace nexigen::core
