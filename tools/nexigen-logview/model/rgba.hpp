// nexigen-logview — RGBA color value shared by the model and the UI.
#pragma once

#include <format>
#include <string>

#include <nlohmann/json.hpp>

namespace nexigen::logview {

/// @brief Linear-ish RGBA color in the `0.0..1.0` range used by Dear ImGui.
struct Rgba {
    float r{1.0F}; ///< Red.
    float g{1.0F}; ///< Green.
    float b{1.0F}; ///< Blue.
    float a{1.0F}; ///< Alpha.

    /// @brief Cold-path diagnostic rendering.
    /// @return Text such as `Rgba(1.000, 0.500, 0.000, 1.000)`.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic mirror of `Rgba::to_string`.
/// @param color Color to render.
/// @return The same text.
[[nodiscard]] inline std::string to_string(const Rgba& color) {
    return color.to_string();
}

/// @brief ADL JSON writer.
/// @param j Destination array.
/// @param color Color to serialize.
inline void to_json(nlohmann::json& j, const Rgba& color) {
    j = nlohmann::json::array({color.r, color.g, color.b, color.a});
}

/// @brief Non-throwing ADL JSON reader.
/// @param j Source array.
/// @param color Destination, untouched on wrong shape/type.
inline void from_json(const nlohmann::json& j, Rgba& color) noexcept {
    if (!j.is_array() || j.size() != 4U) {
        return;
    }
    if (!j[0].is_number() || !j[1].is_number() || !j[2].is_number() || !j[3].is_number()) {
        return;
    }
    color.r = j[0].get<float>();
    color.g = j[1].get<float>();
    color.b = j[2].get<float>();
    color.a = j[3].get<float>();
}

} // namespace nexigen::logview

/// @brief `std::formatter` for `Rgba`.
template <> struct std::formatter<nexigen::logview::Rgba> : std::formatter<std::string_view> {
    /// @brief Format the diagnostic text.
    template <class Ctx> auto format(const nexigen::logview::Rgba& color, Ctx& ctx) const {
        const std::string text = color.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
