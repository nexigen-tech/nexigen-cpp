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
enum class TimeSyncStatus : std::uint8_t {
    Ok,             ///< Within the configured step and rate limits.
    Stepped,        ///< Forward offset jump above `max_step_ns`.
    GoingBackwards, ///< Backward offset jump above `max_step_ns`.
    RateExceeded,   ///< Slew faster than `max_rate_ppm` over the window.
};

/// @brief Canonical name of a time-sync status.
[[nodiscard]] constexpr std::string_view to_string(TimeSyncStatus value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical time-sync status name.
[[nodiscard]] constexpr bool from_string(std::string_view name, TimeSyncStatus& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for a time-sync status.
inline void to_json(nlohmann::json& j, const TimeSyncStatus value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for a time-sync status (non-throwing).
inline void from_json(const nlohmann::json& j, TimeSyncStatus& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::chrono

namespace nexigen::core {

/// @brief `TimeSyncStatus` vocabulary table.
template <> struct EnumTraits<nexigen::chrono::TimeSyncStatus> {
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
template <>
struct std::formatter<nexigen::chrono::TimeSyncStatus> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::chrono::TimeSyncStatus value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::chrono::to_string(value), ctx);
    }
};
