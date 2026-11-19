// nexigen::chrono — how a hardware domain obtains its correlation pair.
//
// The access mode is configuration, not a driver detail: a deployment declares
// exactly one mode per hardware domain so the daemon knows whether to open a
// PTP clock, follow the latched register contract, or read a legacy counter in
// software.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::chrono {

/// @brief How a backend obtains its correlation pair.
///
/// @note Spec `chrono/time-base`, requirement "Hardware domain access modes".
enum class DeviceAccessMode : std::uint8_t {
    PhcCrossTimestamp, ///< Driver-registered PTP clock (`/dev/ptpN`).
    RegisterPair,      ///< Latched register pair (D1-D3 contract).
    ManualPair,        ///< Software-read pair, no latch or PTM.
};

/// @brief Canonical name of a device access mode.
///
/// @param value Mode to name.
/// @return `phc_cross_timestamp`, `register_pair`, or `manual_pair`.
[[nodiscard]] constexpr std::string_view to_string(DeviceAccessMode value) noexcept {
    switch (value) {
        case DeviceAccessMode::PhcCrossTimestamp: return "phc_cross_timestamp";
        case DeviceAccessMode::RegisterPair:      return "register_pair";
        case DeviceAccessMode::ManualPair:        return "manual_pair";
    }
    return "manual_pair";
}

/// @brief Parse a device access mode name.
///
/// @param name Candidate name.
/// @param out Receives the mode only when the name matches.
/// @return `true` when parsed; `false` leaves `out` unchanged.
[[nodiscard]] constexpr bool from_string(std::string_view name, DeviceAccessMode& out) noexcept {
    if (name == "phc_cross_timestamp") {
        out = DeviceAccessMode::PhcCrossTimestamp;
        return true;
    }
    if (name == "register_pair") {
        out = DeviceAccessMode::RegisterPair;
        return true;
    }
    if (name == "manual_pair") {
        out = DeviceAccessMode::ManualPair;
        return true;
    }
    return false;
}

/// @brief ADL JSON writer for a device access mode.
///
/// @param j Destination JSON value.
/// @param value Mode to serialize.
inline void to_json(nlohmann::json& j, const DeviceAccessMode value) {
    j = to_string(value);
}

/// @brief ADL JSON reader for a device access mode (non-throwing).
///
/// @param j Source JSON value.
/// @param out Receives the mode; unchanged when the value is not a known name.
inline void from_json(const nlohmann::json& j, DeviceAccessMode& out) noexcept {
    if (!j.is_string()) {
        return;
    }
    static_cast<void>(from_string(j.get_ref<const std::string&>(), out));
}

} // namespace nexigen::chrono

/// @brief `std::formatter` for `DeviceAccessMode`.
template <>
struct std::formatter<nexigen::chrono::DeviceAccessMode> : std::formatter<std::string_view> {
    /// @brief Format the mode through `to_string()`.
    template <class Ctx>
    auto format(const nexigen::chrono::DeviceAccessMode value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::chrono::to_string(value), ctx);
    }
};
