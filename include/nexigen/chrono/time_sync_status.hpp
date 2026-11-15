// nexigen::chrono — time-sync violation vocabulary (change
// time-sync-supervision).
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::chrono {

/// @brief Classification of one realtime/monotonic offset transition.
///
/// `TimeSyncMonitor::classify` and `TimeSyncMonitor::sample` map every offset
/// change to exactly one of these values. The vocabulary is the observable
/// contract of time-base supervision: an application (or PHM) reacts to a
/// non-`Ok` value by logging, degrading, or resetting, while the monitor itself
/// never acts.
///
/// The enum is a fixed-size `std::uint8_t`, so a sample or a status can travel
/// through shared memory and be serialized without allocation.
///
/// @note Spec `chrono/time-sync`, requirement "Time synchronization
///       classification".
enum class TimeSyncStatus : std::uint8_t {
    Ok,             ///< Within the configured step and rate limits.
    Stepped,        ///< Forward offset jump above `max_step_ns`.
    GoingBackwards, ///< Backward offset jump above `max_step_ns`.
    RateExceeded,   ///< Slew faster than `max_rate_ppm` over the window.
};

/// @brief Canonical name of a time-sync status.
///
/// @param value Status to name.
/// @return Stable English name (`Ok`, `Stepped`, `GoingBackwards`,
///         `RateExceeded`) borrowed from the vocabulary table.
/// @note Spec `chrono/time-sync`, requirement "Monitor diagnostics".
[[nodiscard]] constexpr std::string_view to_string(TimeSyncStatus value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical time-sync status name.
///
/// @param name Candidate name, as produced by `to_string`.
/// @param out Receives the status only when the name matches.
/// @return True when `name` named a status; false leaves `out` unchanged.
/// @note Spec `chrono/time-sync`, requirement "Monitor diagnostics".
[[nodiscard]] constexpr bool from_string(std::string_view name, TimeSyncStatus& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for a time-sync status.
///
/// @param j Object that receives the canonical name.
/// @param value Status to serialize.
/// @note Mandate 12 (JSON through nlohmann); spec `chrono/time-sync`,
///       requirement "Monitor diagnostics".
inline void to_json(nlohmann::json& j, const TimeSyncStatus value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for a time-sync status (non-throwing).
///
/// @param j JSON value holding the canonical name.
/// @param out Receives the parsed status; unchanged when parsing fails.
/// @note Mandate 12; spec `chrono/time-sync`, requirement "Monitor
///       diagnostics".
inline void from_json(const nlohmann::json& j, TimeSyncStatus& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::chrono

namespace nexigen::core {

/// @brief `TimeSyncStatus` vocabulary table.
template <> struct EnumTraits<nexigen::chrono::TimeSyncStatus> {
    /// @brief One row of the status vocabulary.
    struct Entry {
        nexigen::chrono::TimeSyncStatus value; ///< Enumerator.
        std::string_view name;                 ///< Canonical name.
    };
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::chrono::TimeSyncStatus::Ok, "Ok"},
        {nexigen::chrono::TimeSyncStatus::Stepped, "Stepped"},
        {nexigen::chrono::TimeSyncStatus::GoingBackwards, "GoingBackwards"},
        {nexigen::chrono::TimeSyncStatus::RateExceeded, "RateExceeded"},
    }};
};

} // namespace nexigen::core

/// @brief `std::formatter` for a time-sync status (project mandate 11).
///
/// Makes `std::format("{}", status)` equal `to_string(status)`.
///
/// @note Mandate 11 (universal `std::formatter`); spec `chrono/time-sync`,
///       requirement "Monitor diagnostics".
template <>
struct std::formatter<nexigen::chrono::TimeSyncStatus> : std::formatter<std::string_view> {
    /// @brief Write the canonical status name into the format context.
    ///
    /// @tparam Ctx `std::format` context type, for example
    ///         `std::format_context`.
    /// @param value Status to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    /// @note Mandate 11; spec `chrono/time-sync`, requirement "Monitor
    ///       diagnostics".
    template <class Ctx> auto format(const nexigen::chrono::TimeSyncStatus value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::chrono::to_string(value), ctx);
    }
};
