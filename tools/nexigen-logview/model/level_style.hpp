// nexigen-logview — text and optional background color for one log level.
#pragma once

#include <format>
#include <string>

#include <nlohmann/json.hpp>

#include "rgba.hpp"

namespace nexigen::logview {

/// @brief Colors applied to one severity.
struct LevelStyle {
    Rgba text{};       ///< Foreground color.
    Rgba background{}; ///< Background color; alpha zero means no fill.

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with both colors.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic mirror of `LevelStyle::to_string`.
/// @param style Style to render.
/// @return The same text.
[[nodiscard]] inline std::string to_string(const LevelStyle& style) {
    return style.to_string();
}

/// @brief ADL JSON writer.
/// @param j Destination object.
/// @param style Style to serialize.
inline void to_json(nlohmann::json& j, const LevelStyle& style) {
    j = nlohmann::json{{"text", style.text}, {"background", style.background}};
}

/// @brief Non-throwing ADL JSON reader.
/// @param j Source object.
/// @param style Destination, fields untouched when absent or ill-typed.
inline void from_json(const nlohmann::json& j, LevelStyle& style) noexcept {
    if (!j.is_object()) {
        return;
    }
    if (const auto it = j.find("text"); it != j.end()) {
        from_json(*it, style.text);
    }
    if (const auto it = j.find("background"); it != j.end()) {
        from_json(*it, style.background);
    }
}

} // namespace nexigen::logview

/// @brief `std::formatter` for `LevelStyle`.
template <> struct std::formatter<nexigen::logview::LevelStyle> : std::formatter<std::string_view> {
    /// @brief Format the diagnostic text.
    template <class Ctx> auto format(const nexigen::logview::LevelStyle& style, Ctx& ctx) const {
        const std::string text = style.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
