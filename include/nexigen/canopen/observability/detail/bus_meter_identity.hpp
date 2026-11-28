// nexigen::canopen::observability::detail — per-bus meter identity (change
// add-canopen-observability; design D2).
#pragma once

#include <string>
#include <string_view>

namespace nexigen::canopen::observability::detail {

/// @brief Name/version pair identifying one bus meter.
///
/// The name carries the bus dimension that the metrics API cannot express as a
/// label: `canopen.<bus>` (for example `canopen.can0`).
///
/// @note Spec `canopen/observability`, requirement "One runtime owns one logger
///       and one meter per bus".
struct BusMeterIdentity {
    std::string name{};    ///< Meter name, `canopen.<bus>`.
    std::string version{}; ///< Meter version supplied by the runtime configuration.

    /// @brief Cold-path diagnostic string.
    ///
    /// @return `BusMeterIdentity(name=..., version=...)`.
    /// @note Mandate 9.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `BusMeterIdentity::to_string()`.
///
/// @param identity Identity to render.
/// @return The same text as the member.
/// @note Mandate 9.
[[nodiscard]] inline std::string to_string(const BusMeterIdentity& identity) {
    return identity.to_string();
}

} // namespace nexigen::canopen::observability::detail
