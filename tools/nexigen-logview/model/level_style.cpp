#include "level_style.hpp"

#include <format>

namespace nexigen::logview {

std::string LevelStyle::to_string() const {
    return std::format("LevelStyle(text={}, background={})", text, background);
}

} // namespace nexigen::logview
