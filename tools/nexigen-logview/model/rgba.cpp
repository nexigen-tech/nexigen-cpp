#include "rgba.hpp"

#include <format>

namespace nexigen::logview {

std::string Rgba::to_string() const {
    return std::format("Rgba({:.3f}, {:.3f}, {:.3f}, {:.3f})", r, g, b, a);
}

} // namespace nexigen::logview
