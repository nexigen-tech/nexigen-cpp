// nexigen-canview — tool identity (change add-can-monitor-view).
#pragma once

#include <string_view>

namespace nexigen::canview {

/// @brief Canonical executable name.
inline constexpr std::string_view kToolName{"nexigen-canview"};

/// @brief Tool version reported by `--version`.
[[nodiscard]] std::string_view version() noexcept;

} // namespace nexigen::canview
