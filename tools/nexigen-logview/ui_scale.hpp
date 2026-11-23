// nexigen-logview — one canonical UI-scale contract for CLI and window setup.
#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string_view>
#include <system_error>

namespace nexigen::logview {

/// @brief Smallest explicit scale accepted from the command line.
inline constexpr float min_ui_scale = 0.5F;
/// @brief Largest explicit or automatic scale.
inline constexpr float max_ui_scale = 4.0F;
/// @brief Smallest scale produced by monitor auto-detection.
inline constexpr float min_auto_ui_scale = 1.0F;

/// @brief Parse a strict decimal UI scale.
/// @param text Full command-line token.
/// @param out Destination, untouched on failure.
/// @return `true` when `text` is finite and in `min_ui_scale..max_ui_scale`.
[[nodiscard]] inline bool parse_ui_scale(std::string_view text, float& out) noexcept {
    if (text.empty()) {
        return false;
    }
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    float value = 0.0F;
    const auto result = std::from_chars(begin, end, value, std::chars_format::general);
    if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(value) ||
        value < min_ui_scale || value > max_ui_scale) {
        return false;
    }
    out = value;
    return true;
}

/// @brief Clamp a monitor-reported content scale into the automatic range.
/// @param content_scale Larger primary-monitor content-scale axis.
/// @return Scale in `min_auto_ui_scale..max_ui_scale`.
[[nodiscard]] inline float clamp_auto_ui_scale(float content_scale) noexcept {
    return std::clamp(content_scale, min_auto_ui_scale, max_ui_scale);
}

} // namespace nexigen::logview
