// nexigen-logview — level and application color rules.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/core/log_level.hpp>

#include "color_mode.hpp"
#include "level_style.hpp"
#include "rgba.hpp"

namespace nexigen::logview {

/// @brief Editable level colors and the fixed 256-entry application palette.
class ColorManager {
public:
    /// @brief Number of severity styles (Verbose..Off).
    static constexpr std::size_t level_count = 7;

    /// @brief Construct the built-in engineering theme.
    ColorManager();

    /// @brief Restore every level color and palette entry to its default.
    void reset();

    /// @brief Active row-color strategy.
    /// @return Mode reference.
    [[nodiscard]] ColorMode& mode() noexcept { return mode_; }
    /// @brief Active row-color strategy.
    /// @return Mode value.
    [[nodiscard]] ColorMode mode() const noexcept { return mode_; }
    /// @brief Select the row-color strategy.
    /// @param mode New mode.
    void set_mode(ColorMode mode) noexcept { mode_ = mode; }

    /// @brief Editable style for one level.
    /// @param level Severity; `Off` maps to the last slot.
    /// @return Mutable style reference.
    [[nodiscard]] LevelStyle& level(nexigen::core::LogLevel level) noexcept;
    /// @brief Style for one level.
    /// @param level Severity; `Off` maps to the last slot.
    /// @return Borrowed style.
    [[nodiscard]] const LevelStyle& level(nexigen::core::LogLevel level) const noexcept;

    /// @brief Editable palette entry.
    /// @param index Palette index in `0..255`.
    /// @return Mutable color reference.
    [[nodiscard]] Rgba& palette(std::size_t index) noexcept { return palette_[index & 255U]; }
    /// @brief Palette entry.
    /// @param index Palette index in `0..255`.
    /// @return Borrowed color.
    [[nodiscard]] const Rgba& palette(std::size_t index) const noexcept {
        return palette_[index & 255U];
    }
    /// @brief Application color by registry palette index.
    /// @param index Palette index.
    /// @return Borrowed color.
    [[nodiscard]] const Rgba& app_color(std::uint16_t index) const noexcept {
        return palette_[index % 256U];
    }

    /// @brief Whether a level colors the whole row in criticality mode.
    /// @param level Severity.
    /// @return `true` for Error and Fatal.
    [[nodiscard]] static constexpr bool is_critical(nexigen::core::LogLevel level) noexcept {
        return level == nexigen::core::LogLevel::Error || level == nexigen::core::LogLevel::Fatal;
    }

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with mode and hidden state.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Build the default severity table.
    static std::array<LevelStyle, level_count> default_levels() noexcept;
    /// @brief Build the default application palette.
    static std::array<Rgba, 256> default_palette() noexcept;

    friend void to_json(nlohmann::json& j, const ColorManager& colors);
    friend void from_json(const nlohmann::json& j, ColorManager& colors) noexcept;

    std::array<LevelStyle, level_count> levels_{}; ///< Severity table.
    std::array<Rgba, 256> palette_{};              ///< Application palette.
    ColorMode mode_{ColorMode::Combined};          ///< Row-color strategy.
};

/// @brief Free diagnostic mirror of `ColorManager::to_string`.
/// @param colors Manager to render.
/// @return The same text.
[[nodiscard]] std::string to_string(const ColorManager& colors);

/// @brief ADL JSON writer for a color manager.
/// @param j Destination object.
/// @param colors Manager to serialize.
void to_json(nlohmann::json& j, const ColorManager& colors);

/// @brief Non-throwing ADL JSON reader for a color manager.
/// @param j Source object.
/// @param colors Destination, untouched fields remain.
void from_json(const nlohmann::json& j, ColorManager& colors) noexcept;

} // namespace nexigen::logview

/// @brief `std::formatter` for `ColorManager`.
template <>
struct std::formatter<nexigen::logview::ColorManager> : std::formatter<std::string_view> {
    /// @brief Format the diagnostic text.
    template <class Ctx> auto format(const nexigen::logview::ColorManager& colors, Ctx& ctx) const {
        const std::string text = colors.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
