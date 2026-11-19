// nexigen::chrono — time-base service status vocabulary (change
// add-time-base-service; spec chrono/time-base "Time-base status").
//
// `TimeBaseStatus` is the observable health of the published time base: the
// daemon's own state, not a per-consumer verdict. `Lost` means the published
// coefficients are not applicable at all; `Degraded` means they are applicable
// with growing uncertainty. Consumers derive their own fail-fast decision from
// the status plus the domains they declared as required.
//
// Cold path only: the vocabulary is read when a client evaluates status or
// renders diagnostics, never on a producer hot path. The table is static
// constexpr storage, so naming and parsing allocate nothing.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::chrono {

/// @brief Health of the published time base.
///
/// `NotReady` covers initialization and the first calibration window; `Ready`
/// means every declared domain is locked (or none is declared); `Degraded`
/// means at least one declared domain is absent, free-running or in holdover
/// while the canonical scale itself remains usable; `Lost` means the canonical
/// scale or a required domain is unusable and coefficients must not be applied.
///
/// @note Spec `chrono/time-base`, requirement "Time-base status".
enum class TimeBaseStatus : std::uint8_t {
    NotReady, ///< Initializing or calibrating; no map is trustworthy yet.
    Ready,    ///< Every declared domain is locked (or none is declared).
    Degraded, ///< Canonical scale usable, at least one declared domain is not.
    Lost,     ///< Canonical scale or a required domain is unusable.
};

} // namespace nexigen::chrono

// Vocabulary table for core::enum_name/enum_value (mandates 9-11).
namespace nexigen::core {

/// @brief Vocabulary table wiring `TimeBaseStatus` into the shared enum engine.
///
/// Canonical wire spellings are lowercase snake_case, matching the JSON line
/// vocabulary used by the telemetry streams.
///
/// @note Spec `chrono/time-base`, requirement "Time-base status"; project
///       mandates 9 and 10.
template <> struct EnumTraits<nexigen::chrono::TimeBaseStatus> {
    /// @brief One status-to-name row.
    struct Entry {
        nexigen::chrono::TimeBaseStatus value; ///< Enumerator described by this row.
        std::string_view name;                 ///< Canonical wire spelling.
    };
    /// @brief Append-only rows in enumerator order.
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::chrono::TimeBaseStatus::NotReady, "not_ready"},
        {nexigen::chrono::TimeBaseStatus::Ready, "ready"},
        {nexigen::chrono::TimeBaseStatus::Degraded, "degraded"},
        {nexigen::chrono::TimeBaseStatus::Lost, "lost"},
    }};
};

} // namespace nexigen::core

namespace nexigen::chrono {

/// @brief Canonical name of a time-base status.
///
/// @param value Status to name.
/// @return Static-storage view of the canonical spelling; out-of-table values
///         render `"Unknown"`.
/// @note Spec `chrono/time-base`, requirement "Time-base status"; mandate 9.
[[nodiscard]] constexpr std::string_view to_string(TimeBaseStatus value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical time-base status name.
///
/// Exact, case-sensitive match; `out` is left unchanged on a miss.
///
/// @param name Candidate canonical spelling.
/// @param out Destination enumerator.
/// @return `true` when `name` named a status and `out` was assigned.
/// @note Spec `chrono/time-base`, requirement "Time-base status"; mandate 9.
[[nodiscard]] constexpr bool from_string(std::string_view name, TimeBaseStatus& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for `TimeBaseStatus` (cold path only).
///
/// @param j Destination JSON value, replaced by the canonical name string.
/// @param value Status to serialize.
/// @note Spec `chrono/time-base`, requirement "Time-base status"; mandate 10.
inline void to_json(nlohmann::json& j, const TimeBaseStatus value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for `TimeBaseStatus`, strictly non-throwing.
///
/// @param j Source JSON value.
/// @param out Destination enumerator, untouched unless a known name parses.
/// @note Spec `chrono/time-base`, requirement "Time-base status"; mandate 10.
inline void from_json(const nlohmann::json& j, TimeBaseStatus& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::chrono
