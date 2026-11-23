#include "color_manager.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace nexigen::logview {

namespace {

std::size_t level_index(nexigen::core::LogLevel level) noexcept {
    const auto index = static_cast<std::size_t>(level);
    return index < ColorManager::level_count ? index : ColorManager::level_count - 1U;
}

Rgba hsv(double hue, double saturation, double value) noexcept {
    const double h = hue - std::floor(hue);
    const double sector = h * 6.0;
    const int index = static_cast<int>(sector);
    const double fraction = sector - static_cast<double>(index);
    const double p = value * (1.0 - saturation);
    const double q = value * (1.0 - saturation * fraction);
    const double t = value * (1.0 - saturation * (1.0 - fraction));
    double r = value;
    double g = t;
    double b = p;
    switch (index % 6) {
        case 0:
            r = value;
            g = t;
            b = p;
            break;
        case 1:
            r = q;
            g = value;
            b = p;
            break;
        case 2:
            r = p;
            g = value;
            b = t;
            break;
        case 3:
            r = p;
            g = q;
            b = value;
            break;
        case 4:
            r = t;
            g = p;
            b = value;
            break;
        default:
            r = value;
            g = p;
            b = q;
            break;
    }
    return Rgba{static_cast<float>(r), static_cast<float>(g), static_cast<float>(b), 1.0F};
}

} // namespace

ColorManager::ColorManager() : levels_(default_levels()), palette_(default_palette()) {}

std::array<LevelStyle, ColorManager::level_count> ColorManager::default_levels() noexcept {
    std::array<LevelStyle, level_count> levels{};
    levels[level_index(nexigen::core::LogLevel::Verbose)] = {Rgba{0.62F, 0.64F, 0.68F, 1.0F}, {}};
    levels[level_index(nexigen::core::LogLevel::Debug)] = {Rgba{0.55F, 0.78F, 0.88F, 1.0F}, {}};
    levels[level_index(nexigen::core::LogLevel::Info)] = {Rgba{0.86F, 0.88F, 0.92F, 1.0F}, {}};
    levels[level_index(nexigen::core::LogLevel::Warning)] = {Rgba{1.00F, 0.82F, 0.32F, 1.0F}, {}};
    levels[level_index(nexigen::core::LogLevel::Error)] = {Rgba{1.00F, 0.45F, 0.40F, 1.0F}, {}};
    levels[level_index(nexigen::core::LogLevel::Fatal)] = {Rgba{1.00F, 0.92F, 0.92F, 1.0F},
                                                           Rgba{0.46F, 0.08F, 0.10F, 1.0F}};
    levels[level_index(nexigen::core::LogLevel::Off)] = {Rgba{0.50F, 0.50F, 0.50F, 1.0F}, {}};
    return levels;
}

std::array<Rgba, 256> ColorManager::default_palette() noexcept {
    std::array<Rgba, 256> palette{};
    for (std::size_t i = 0; i < palette.size(); ++i) {
        const double hue = static_cast<double>(i) * 0.6180339887498949;
        const double saturation = i < 12U ? 0.64 : 0.46;
        const double value = i < 12U ? 0.96 : 0.82;
        palette[i] = hsv(hue, saturation, value);
    }
    return palette;
}

void ColorManager::reset() {
    levels_ = default_levels();
    palette_ = default_palette();
    mode_ = ColorMode::Combined;
}

LevelStyle& ColorManager::level(nexigen::core::LogLevel level) noexcept {
    return levels_[level_index(level)];
}

const LevelStyle& ColorManager::level(nexigen::core::LogLevel level) const noexcept {
    return levels_[level_index(level)];
}

std::string ColorManager::to_string() const {
    return std::format("ColorManager(mode={}, levels={}, palette={})", mode_, levels_.size(),
                       palette_.size());
}

std::string to_string(const ColorManager& colors) {
    return colors.to_string();
}

void to_json(nlohmann::json& j, const ColorManager& colors) {
    nlohmann::json levels = nlohmann::json::array();
    for (const LevelStyle& style : colors.levels_) {
        levels.push_back(style);
    }
    nlohmann::json palette = nlohmann::json::array();
    for (const Rgba& color : colors.palette_) {
        palette.push_back(color);
    }
    j = nlohmann::json{{"mode", colors.mode_}, {"levels", levels}, {"palette", palette}};
}

void from_json(const nlohmann::json& j, ColorManager& colors) noexcept {
    if (!j.is_object()) {
        return;
    }
    if (const auto it = j.find("mode"); it != j.end()) {
        from_json(*it, colors.mode_);
    }
    if (const auto it = j.find("levels"); it != j.end() && it->is_array()) {
        const std::size_t n = std::min(it->size(), ColorManager::level_count);
        for (std::size_t i = 0; i < n; ++i) {
            from_json((*it)[i], colors.levels_[i]);
        }
    }
    if (const auto it = j.find("palette"); it != j.end() && it->is_array()) {
        const std::size_t n = std::min(it->size(), colors.palette_.size());
        for (std::size_t i = 0; i < n; ++i) {
            from_json((*it)[i], colors.palette_[i]);
        }
    }
}

} // namespace nexigen::logview
