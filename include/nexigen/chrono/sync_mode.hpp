// nexigen::chrono — platform synchronization mode (change
// add-chrono-service-contract; spec chrono/time-sync).
//
// The mode names the master clock the platform follows:
//   Standalone — local CPU oscillator / RTC; no external grandmaster.
//   PtpSynced  — external IEEE 1588 grandmaster reached through the NIC PHC
//                (ptp4l + phc2sys discipline CLOCK_TAI/CLOCK_REALTIME).
//   Holdover   — the last locked local-oscillator drift model after PTP loss.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::chrono {

/// @brief Active synchronization mode and master clock.
enum class SyncMode : std::uint8_t {
    Standalone, ///< Master: local CPU oscillator / RTC.
    PtpSynced,  ///< Master: external IEEE 1588 grandmaster via the NIC PHC.
    Holdover,   ///< Master: last locked local-oscillator drift model.
};

} // namespace nexigen::chrono

namespace nexigen::core {

/// @brief Vocabulary table for `SyncMode`.
template <> struct EnumTraits<nexigen::chrono::SyncMode> {
    /// @brief One mode-to-name row.
    struct Entry {
        nexigen::chrono::SyncMode value; ///< Enumerator.
        std::string_view name;           ///< Canonical spelling.
    };
    /// @brief Append-only rows.
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::chrono::SyncMode::Standalone, "standalone"},
        {nexigen::chrono::SyncMode::PtpSynced, "ptp_synced"},
        {nexigen::chrono::SyncMode::Holdover, "holdover"},
    }};
};

} // namespace nexigen::core

namespace nexigen::chrono {

/// @brief Canonical name of a synchronization mode.
[[nodiscard]] constexpr std::string_view to_string(SyncMode value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a synchronization-mode name.
[[nodiscard]] constexpr bool from_string(std::string_view name, SyncMode& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for `SyncMode`.
inline void to_json(nlohmann::json& j, const SyncMode value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for `SyncMode`, non-throwing.
inline void from_json(const nlohmann::json& j, SyncMode& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::chrono

/// @brief `std::formatter` for `SyncMode`.
template <> struct std::formatter<nexigen::chrono::SyncMode> : std::formatter<std::string_view> {
    /// @brief Format the mode through its canonical name.
    ///
    /// @tparam Ctx Format context type.
    /// @param value Mode to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator past the last written character.
    /// @note Mandate 11 (`std::formatter`); spec `chrono/time-sync`,
    ///       requirement "Platform sync mode".
    template <class Ctx> auto format(const nexigen::chrono::SyncMode value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::chrono::to_string(value), ctx);
    }
};
