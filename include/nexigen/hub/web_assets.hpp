// nexigen::hub — embedded web assets
// (change nexigen-hub; spec hub/web-ui).
#pragma once

#include <string_view>

namespace nexigen::hub {

[[nodiscard]] std::string_view index_html() noexcept;
[[nodiscard]] std::string_view app_js() noexcept;
[[nodiscard]] std::string_view style_css() noexcept;

} // namespace nexigen::hub
